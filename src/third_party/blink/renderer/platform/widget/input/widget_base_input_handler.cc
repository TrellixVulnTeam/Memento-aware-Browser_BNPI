// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/widget/input/widget_base_input_handler.h"

#include <stddef.h>
#include <stdint.h>
#include <utility>

#include "base/metrics/histogram_macros.h"
#include "build/build_config.h"
#include "cc/metrics/event_metrics.h"
#include "cc/paint/element_id.h"
#include "cc/trees/latency_info_swap_promise_monitor.h"
#include "cc/trees/layer_tree_host.h"
#include "services/tracing/public/cpp/perfetto/flow_event_utils.h"
#include "services/tracing/public/cpp/perfetto/macros.h"
#include "third_party/blink/public/common/input/web_gesture_device.h"
#include "third_party/blink/public/common/input/web_gesture_event.h"
#include "third_party/blink/public/common/input/web_input_event_attribution.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "third_party/blink/public/common/input/web_pointer_event.h"
#include "third_party/blink/public/common/input/web_touch_event.h"
#include "third_party/blink/public/mojom/input/input_event_result.mojom-shared.h"
#include "third_party/blink/public/platform/scheduler/web_thread_scheduler.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_frame_widget.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "third_party/blink/renderer/platform/widget/widget_base.h"
#include "third_party/blink/renderer/platform/widget/widget_base_client.h"
#include "ui/latency/latency_info.h"

#if defined(OS_ANDROID)
#include <android/keycodes.h>
#endif

using perfetto::protos::pbzero::ChromeLatencyInfo;
using perfetto::protos::pbzero::TrackEvent;

namespace blink {

namespace {

int64_t GetEventLatencyMicros(base::TimeTicks event_timestamp,
                              base::TimeTicks now) {
  return (now - event_timestamp).InMicroseconds();
}

void LogInputEventLatencyUma(const WebInputEvent& event, base::TimeTicks now) {
  UMA_HISTOGRAM_CUSTOM_COUNTS(
      "Event.AggregatedLatency.Renderer2",
      base::saturated_cast<base::HistogramBase::Sample>(
          GetEventLatencyMicros(event.TimeStamp(), now)),
      1, 10000000, 100);
}

void LogPassiveEventListenersUma(WebInputEventResult result,
                                 WebInputEvent::DispatchType dispatch_type) {
  // This enum is backing a histogram. Do not remove or reorder members.
  enum ListenerEnum {
    PASSIVE_LISTENER_UMA_ENUM_PASSIVE,
    PASSIVE_LISTENER_UMA_ENUM_UNCANCELABLE,
    PASSIVE_LISTENER_UMA_ENUM_SUPPRESSED,
    PASSIVE_LISTENER_UMA_ENUM_CANCELABLE,
    PASSIVE_LISTENER_UMA_ENUM_CANCELABLE_AND_CANCELED,
    PASSIVE_LISTENER_UMA_ENUM_FORCED_NON_BLOCKING_DUE_TO_FLING,
    PASSIVE_LISTENER_UMA_ENUM_FORCED_NON_BLOCKING_DUE_TO_MAIN_THREAD_RESPONSIVENESS_DEPRECATED,
    PASSIVE_LISTENER_UMA_ENUM_COUNT
  };

  ListenerEnum enum_value;
  switch (dispatch_type) {
    case WebInputEvent::DispatchType::kListenersForcedNonBlockingDueToFling:
      enum_value = PASSIVE_LISTENER_UMA_ENUM_FORCED_NON_BLOCKING_DUE_TO_FLING;
      break;
    case WebInputEvent::DispatchType::kListenersNonBlockingPassive:
      enum_value = PASSIVE_LISTENER_UMA_ENUM_PASSIVE;
      break;
    case WebInputEvent::DispatchType::kEventNonBlocking:
      enum_value = PASSIVE_LISTENER_UMA_ENUM_UNCANCELABLE;
      break;
    case WebInputEvent::DispatchType::kBlocking:
      if (result == WebInputEventResult::kHandledApplication)
        enum_value = PASSIVE_LISTENER_UMA_ENUM_CANCELABLE_AND_CANCELED;
      else if (result == WebInputEventResult::kHandledSuppressed)
        enum_value = PASSIVE_LISTENER_UMA_ENUM_SUPPRESSED;
      else
        enum_value = PASSIVE_LISTENER_UMA_ENUM_CANCELABLE;
      break;
    default:
      NOTREACHED();
      return;
  }

  UMA_HISTOGRAM_ENUMERATION("Event.PassiveListeners", enum_value,
                            PASSIVE_LISTENER_UMA_ENUM_COUNT);
}

void LogAllPassiveEventListenersUma(const WebInputEvent& input_event,
                                    WebInputEventResult result) {
  // TODO(dtapuska): Use the input_event.timeStampSeconds as the start
  // ideally this should be when the event was sent by the compositor to the
  // renderer. https://crbug.com/565348.
  if (input_event.GetType() == WebInputEvent::Type::kTouchStart ||
      input_event.GetType() == WebInputEvent::Type::kTouchMove ||
      input_event.GetType() == WebInputEvent::Type::kTouchEnd) {
    const WebTouchEvent& touch = static_cast<const WebTouchEvent&>(input_event);

    LogPassiveEventListenersUma(result, touch.dispatch_type);
  } else if (input_event.GetType() == WebInputEvent::Type::kMouseWheel) {
    LogPassiveEventListenersUma(
        result,
        static_cast<const WebMouseWheelEvent&>(input_event).dispatch_type);
  }
}

WebCoalescedInputEvent GetCoalescedWebPointerEventForTouch(
    const WebPointerEvent& pointer_event,
    const std::vector<std::unique_ptr<WebInputEvent>>& coalesced_events,
    const std::vector<std::unique_ptr<WebInputEvent>>& predicted_events,
    const ui::LatencyInfo& latency) {
  std::vector<std::unique_ptr<WebInputEvent>> related_pointer_events;
  for (const std::unique_ptr<WebInputEvent>& event : coalesced_events) {
    DCHECK(WebInputEvent::IsTouchEventType(event->GetType()));
    const WebTouchEvent& touch_event =
        static_cast<const WebTouchEvent&>(*event);
    for (unsigned i = 0; i < touch_event.touches_length; ++i) {
      if (touch_event.touches[i].id == pointer_event.id &&
          touch_event.touches[i].state !=
              WebTouchPoint::State::kStateStationary) {
        related_pointer_events.emplace_back(std::make_unique<WebPointerEvent>(
            touch_event, touch_event.touches[i]));
      }
    }
  }
  std::vector<std::unique_ptr<WebInputEvent>> predicted_pointer_events;
  for (const std::unique_ptr<WebInputEvent>& event : predicted_events) {
    DCHECK(WebInputEvent::IsTouchEventType(event->GetType()));
    const WebTouchEvent& touch_event =
        static_cast<const WebTouchEvent&>(*event);
    for (unsigned i = 0; i < touch_event.touches_length; ++i) {
      if (touch_event.touches[i].id == pointer_event.id &&
          touch_event.touches[i].state !=
              WebTouchPoint::State::kStateStationary) {
        predicted_pointer_events.emplace_back(std::make_unique<WebPointerEvent>(
            touch_event, touch_event.touches[i]));
      }
    }
  }

  return WebCoalescedInputEvent(pointer_event.Clone(),
                                std::move(related_pointer_events),
                                std::move(predicted_pointer_events), latency);
}

mojom::InputEventResultState GetAckResult(WebInputEventResult processed) {
  return processed == WebInputEventResult::kNotHandled
             ? mojom::InputEventResultState::kNotConsumed
             : mojom::InputEventResultState::kConsumed;
}

bool IsGestureScroll(WebInputEvent::Type type) {
  switch (type) {
    case WebGestureEvent::Type::kGestureScrollBegin:
    case WebGestureEvent::Type::kGestureScrollUpdate:
    case WebGestureEvent::Type::kGestureScrollEnd:
      return true;
    default:
      return false;
  }
}

gfx::PointF PositionInWidgetFromInputEvent(const WebInputEvent& event) {
  if (WebInputEvent::IsMouseEventType(event.GetType())) {
    return static_cast<const WebMouseEvent&>(event).PositionInWidget();
  } else if (WebInputEvent::IsGestureEventType(event.GetType())) {
    return static_cast<const WebGestureEvent&>(event).PositionInWidget();
  } else {
    return gfx::PointF(0, 0);
  }
}

bool IsTouchStartOrMove(const WebInputEvent& event) {
  if (WebInputEvent::IsPointerEventType(event.GetType())) {
    return static_cast<const WebPointerEvent&>(event)
        .touch_start_or_first_touch_move;
  } else if (WebInputEvent::IsTouchEventType(event.GetType())) {
    return static_cast<const WebTouchEvent&>(event)
        .touch_start_or_first_touch_move;
  } else {
    return false;
  }
}

}  // namespace

// This class should be placed on the stack when handling an input event. It
// stores information from callbacks from blink while handling an input event
// and allows them to be returned in the InputEventAck result.
class WidgetBaseInputHandler::HandlingState {
 public:
  HandlingState(base::WeakPtr<WidgetBaseInputHandler> input_handler_param,
                bool is_touch_start_or_move)
      : touch_start_or_move(is_touch_start_or_move),
        input_handler(std::move(input_handler_param)) {
    previous_was_handling_input = input_handler->handling_input_event_;
    previous_state = input_handler->handling_input_state_;
    input_handler->handling_input_event_ = true;
    input_handler->handling_input_state_ = this;
  }

  ~HandlingState() {
    // Unwinding the HandlingState on the stack might result in an
    // input_handler_ that got destroyed. i.e. via a nested event loop.
    if (!input_handler)
      return;
    input_handler->handling_input_event_ = previous_was_handling_input;
    DCHECK_EQ(input_handler->handling_input_state_, this);
    input_handler->handling_input_state_ = previous_state;

#if defined(OS_ANDROID)
    if (show_virtual_keyboard)
      input_handler->ShowVirtualKeyboard();
    else
      input_handler->UpdateTextInputState();
#endif
  }

  // Used to intercept overscroll notifications while an event is being
  // handled. If the event causes overscroll, the overscroll metadata can be
  // bundled in the event ack, saving an IPC.  Note that we must continue
  // supporting overscroll IPC notifications due to fling animation updates.
  std::unique_ptr<InputHandlerProxy::DidOverscrollParams> event_overscroll;

  base::Optional<WebTouchAction> touch_action;

  // Used to hold a sequence of parameters corresponding to scroll gesture
  // events that should be injected once the current input event is done
  // being processed.
  std::vector<WidgetBaseInputHandler::InjectScrollGestureParams>
      injected_scroll_params;

  // Whether the event we are handling is a touch start or move.
  bool touch_start_or_move;

#if defined(OS_ANDROID)
  // Whether to show the virtual keyboard or not at the end of processing.
  bool show_virtual_keyboard = false;
#endif

 private:
  HandlingState* previous_state;
  bool previous_was_handling_input;
  base::WeakPtr<WidgetBaseInputHandler> input_handler;
};

WidgetBaseInputHandler::WidgetBaseInputHandler(WidgetBase* widget)
    : widget_(widget),
      supports_buffered_touch_(
          widget_->client()->SupportsBufferedTouchEvents()) {}

WebInputEventResult WidgetBaseInputHandler::HandleTouchEvent(
    const WebCoalescedInputEvent& coalesced_event) {
  const WebInputEvent& input_event = coalesced_event.Event();

  if (input_event.GetType() == WebInputEvent::Type::kTouchScrollStarted) {
    WebPointerEvent pointer_event =
        WebPointerEvent::CreatePointerCausesUaActionEvent(
            WebPointerProperties::PointerType::kUnknown,
            input_event.TimeStamp());
    return widget_->client()->HandleInputEvent(
        WebCoalescedInputEvent(pointer_event, coalesced_event.latency_info()));
  }

  const WebTouchEvent touch_event =
      static_cast<const WebTouchEvent&>(input_event);
  for (unsigned i = 0; i < touch_event.touches_length; ++i) {
    const WebTouchPoint& touch_point = touch_event.touches[i];
    if (touch_point.state != WebTouchPoint::State::kStateStationary) {
      const WebPointerEvent& pointer_event =
          WebPointerEvent(touch_event, touch_point);
      const WebCoalescedInputEvent& coalesced_pointer_event =
          GetCoalescedWebPointerEventForTouch(
              pointer_event, coalesced_event.GetCoalescedEventsPointers(),
              coalesced_event.GetPredictedEventsPointers(),
              coalesced_event.latency_info());
      widget_->client()->HandleInputEvent(coalesced_pointer_event);
    }
  }
  return widget_->client()->DispatchBufferedTouchEvents();
}

void WidgetBaseInputHandler::HandleInputEvent(
    const WebCoalescedInputEvent& coalesced_event,
    HandledEventCallback callback) {
  const WebInputEvent& input_event = coalesced_event.Event();

  // Keep a WeakPtr to this WidgetBaseInputHandler to detect if executing the
  // input event destroyed the associated RenderWidget (and this handler).
  base::WeakPtr<WidgetBaseInputHandler> weak_self =
      weak_ptr_factory_.GetWeakPtr();
  HandlingState handling_state(weak_self, IsTouchStartOrMove(input_event));

  base::TimeTicks start_time;
  if (base::TimeTicks::IsHighResolution())
    start_time = base::TimeTicks::Now();

  TRACE_EVENT1("renderer,benchmark,rail",
               "WidgetBaseInputHandler::OnHandleInputEvent", "event",
               WebInputEvent::GetName(input_event.GetType()));
  int64_t trace_id = coalesced_event.latency_info().trace_id();
  TRACE_EVENT("input,benchmark", "LatencyInfo.Flow",
              [trace_id](perfetto::EventContext ctx) {
                ChromeLatencyInfo* info =
                    ctx.event()->set_chrome_latency_info();
                info->set_trace_id(trace_id);
                info->set_step(ChromeLatencyInfo::STEP_HANDLE_INPUT_EVENT_MAIN);
                tracing::FillFlowEvent(ctx, TrackEvent::LegacyEvent::FLOW_INOUT,
                                       trace_id);
              });

  // If we don't have a high res timer, these metrics won't be accurate enough
  // to be worth collecting. Note that this does introduce some sampling bias.
  if (!start_time.is_null())
    LogInputEventLatencyUma(input_event, start_time);

  ui::LatencyInfo swap_latency_info(coalesced_event.latency_info());
  swap_latency_info.AddLatencyNumber(
      ui::LatencyComponentType::INPUT_EVENT_LATENCY_RENDERER_MAIN_COMPONENT);
  cc::LatencyInfoSwapPromiseMonitor swap_promise_monitor(
      &swap_latency_info, widget_->LayerTreeHost()->GetSwapPromiseManager(),
      nullptr);
  auto scoped_event_metrics_monitor =
      widget_->LayerTreeHost()->GetScopedEventMetricsMonitor(
          cc::EventMetrics::Create(input_event.GetTypeAsUiEventType(),
                                   input_event.TimeStamp(),
                                   input_event.GetScrollInputType()));

  bool prevent_default = false;
  bool show_virtual_keyboard_for_mouse = false;
  if (WebInputEvent::IsMouseEventType(input_event.GetType())) {
    const WebMouseEvent& mouse_event =
        static_cast<const WebMouseEvent&>(input_event);
    TRACE_EVENT2("renderer", "HandleMouseMove", "x",
                 mouse_event.PositionInWidget().x(), "y",
                 mouse_event.PositionInWidget().y());

    prevent_default = widget_->client()->WillHandleMouseEvent(mouse_event);

    // Reset the last known cursor if mouse has left this widget. So next
    // time that the mouse enters we always set the cursor accordingly.
    if (mouse_event.GetType() == WebInputEvent::Type::kMouseLeave)
      current_cursor_.reset();

    if (mouse_event.button == WebPointerProperties::Button::kLeft &&
        mouse_event.GetType() == WebInputEvent::Type::kMouseUp) {
      show_virtual_keyboard_for_mouse = true;
    }
  }

  if (WebInputEvent::IsKeyboardEventType(input_event.GetType())) {
#if defined(OS_ANDROID)
    // The DPAD_CENTER key on Android has a dual semantic: (1) in the general
    // case it should behave like a select key (i.e. causing a click if a button
    // is focused). However, if a text field is focused (2), its intended
    // behavior is to just show the IME and don't propagate the key.
    // A typical use case is a web form: the DPAD_CENTER should bring up the IME
    // when clicked on an input text field and cause the form submit if clicked
    // when the submit button is focused, but not vice-versa.
    // The UI layer takes care of translating DPAD_CENTER into a RETURN key,
    // but at this point we have to swallow the event for the scenario (2).
    const WebKeyboardEvent& key_event =
        static_cast<const WebKeyboardEvent&>(input_event);
    if (key_event.native_key_code == AKEYCODE_DPAD_CENTER &&
        widget_->client()->GetTextInputType() !=
            WebTextInputType::kWebTextInputTypeNone) {
      // Show the keyboard on keyup (not keydown) to match the behavior of
      // Android's TextView.
      if (key_event.GetType() == WebInputEvent::Type::kKeyUp)
        widget_->ShowVirtualKeyboardOnElementFocus();
      // Prevent default for both keydown and keyup (letting the keydown go
      // through to the web app would cause compatibility problems since
      // DPAD_CENTER is also used as a "confirm" button).
      prevent_default = true;
    }
#endif
  }

  if (WebInputEvent::IsGestureEventType(input_event.GetType())) {
    const WebGestureEvent& gesture_event =
        static_cast<const WebGestureEvent&>(input_event);
    prevent_default = prevent_default ||
                      widget_->client()->WillHandleGestureEvent(gesture_event);
  }

  WebInputEventResult processed = prevent_default
                                      ? WebInputEventResult::kHandledSuppressed
                                      : WebInputEventResult::kNotHandled;
  if (input_event.GetType() != WebInputEvent::Type::kChar ||
      !suppress_next_char_events_) {
    suppress_next_char_events_ = false;
    if (processed == WebInputEventResult::kNotHandled) {
      if (supports_buffered_touch_ &&
          WebInputEvent::IsTouchEventType(input_event.GetType()))
        processed = HandleTouchEvent(coalesced_event);
      else
        processed = widget_->client()->HandleInputEvent(coalesced_event);
    }

    // The associated WidgetBase (and this WidgetBaseInputHandler) could
    // have been destroyed. If it was return early before accessing any more of
    // this class.
    if (!weak_self) {
      if (callback) {
        std::move(callback).Run(GetAckResult(processed), swap_latency_info,
                                std::move(handling_state.event_overscroll),
                                handling_state.touch_action);
      }
      return;
    }
  }

  // Handling |input_event| is finished and further down, we might start
  // handling injected scroll events. So, stop monitoring EventMetrics for
  // |input_event| to avoid nested monitors.
  scoped_event_metrics_monitor = nullptr;

  LogAllPassiveEventListenersUma(input_event, processed);

  // If this RawKeyDown event corresponds to a browser keyboard shortcut and
  // it's not processed by webkit, then we need to suppress the upcoming Char
  // events.
  bool is_keyboard_shortcut =
      input_event.GetType() == WebInputEvent::Type::kRawKeyDown &&
      static_cast<const WebKeyboardEvent&>(input_event).is_browser_shortcut;
  if (processed == WebInputEventResult::kNotHandled && is_keyboard_shortcut)
    suppress_next_char_events_ = true;

  // The handling of some input events on the main thread may require injecting
  // scroll gestures back into blink, e.g., a mousedown on a scrollbar. We
  // do this here so that we can attribute latency information from the mouse as
  // a scroll interaction, instead of just classifying as mouse input.
  if (handling_state.injected_scroll_params.size()) {
    HandleInjectedScrollGestures(
        std::move(handling_state.injected_scroll_params), input_event,
        coalesced_event.latency_info());
  }

  // Send gesture scroll events and their dispositions to the compositor thread,
  // so that they can be used to produce the elastic overscroll effect.
  if (input_event.GetType() == WebInputEvent::Type::kGestureScrollBegin ||
      input_event.GetType() == WebInputEvent::Type::kGestureScrollEnd ||
      input_event.GetType() == WebInputEvent::Type::kGestureScrollUpdate) {
    const WebGestureEvent& gesture_event =
        static_cast<const WebGestureEvent&>(input_event);
    if (gesture_event.SourceDevice() == WebGestureDevice::kTouchpad ||
        gesture_event.SourceDevice() == WebGestureDevice::kTouchscreen) {
      gfx::Vector2dF latest_overscroll_delta =
          handling_state.event_overscroll
              ? handling_state.event_overscroll->latest_overscroll_delta
              : gfx::Vector2dF();
      cc::OverscrollBehavior overscroll_behavior =
          handling_state.event_overscroll
              ? handling_state.event_overscroll->overscroll_behavior
              : cc::OverscrollBehavior();
      widget_->client()->ObserveGestureEventAndResult(
          gesture_event, latest_overscroll_delta, overscroll_behavior,
          processed != WebInputEventResult::kNotHandled);
    }
  }

  if (callback) {
    std::move(callback).Run(GetAckResult(processed), swap_latency_info,
                            std::move(handling_state.event_overscroll),
                            handling_state.touch_action);
  } else {
    DCHECK(!handling_state.event_overscroll)
        << "Unexpected overscroll for un-acked event";
  }

  // Show the virtual keyboard if enabled and a user gesture triggers a focus
  // change.
  if ((processed != WebInputEventResult::kNotHandled &&
       input_event.GetType() == WebInputEvent::Type::kTouchEnd) ||
      show_virtual_keyboard_for_mouse) {
    ShowVirtualKeyboard();
  }

  if (!prevent_default &&
      WebInputEvent::IsKeyboardEventType(input_event.GetType()))
    widget_->client()->DidHandleKeyEvent();

// TODO(rouslan): Fix ChromeOS and Windows 8 behavior of autofill popup with
// virtual keyboard.
#if !defined(OS_ANDROID)
  // Virtual keyboard is not supported, so react to focus change immediately.
  if ((processed != WebInputEventResult::kNotHandled &&
       input_event.GetType() == WebInputEvent::Type::kMouseDown) ||
      input_event.GetType() == WebInputEvent::Type::kGestureTap) {
    widget_->client()->FocusChangeComplete();
  }
#endif

  // Ensure all injected scrolls were handled or queue up - any remaining
  // injected scrolls at this point would not be processed.
  DCHECK(handling_state.injected_scroll_params.empty());
}

bool WidgetBaseInputHandler::DidOverscrollFromBlink(
    const gfx::Vector2dF& overscroll_delta,
    const gfx::Vector2dF& accumulated_overscroll,
    const gfx::PointF& position,
    const gfx::Vector2dF& velocity,
    const cc::OverscrollBehavior& behavior) {
  // We aren't currently handling an event. Allow the processing to be
  // dispatched separately from the ACK.
  if (!handling_input_state_)
    return true;

  // If we're currently handling an event, stash the overscroll data such that
  // it can be bundled in the event ack.
  std::unique_ptr<InputHandlerProxy::DidOverscrollParams> params =
      std::make_unique<InputHandlerProxy::DidOverscrollParams>();
  params->accumulated_overscroll = accumulated_overscroll;
  params->latest_overscroll_delta = overscroll_delta;
  params->current_fling_velocity = velocity;
  params->causal_event_viewport_point = position;
  params->overscroll_behavior = behavior;
  handling_input_state_->event_overscroll = std::move(params);
  return false;
}

void WidgetBaseInputHandler::InjectGestureScrollEvent(
    WebGestureDevice device,
    const gfx::Vector2dF& delta,
    ui::ScrollGranularity granularity,
    cc::ElementId scrollable_area_element_id,
    WebInputEvent::Type injected_type) {
  DCHECK(IsGestureScroll(injected_type));
  // If we're currently handling an input event, cache the appropriate
  // parameters so we can dispatch the events directly once blink finishes
  // handling the event.
  // Otherwise, queue the event on the main thread event queue.
  // The latter may occur when scrollbar scrolls are injected due to
  // autoscroll timer - i.e. not within the handling of a mouse event.
  // We don't always just enqueue events, since events queued to the
  // MainThreadEventQueue in the middle of dispatch (which we are) won't
  // be dispatched until the next time the queue gets to run. The side effect
  // of that would be an extra frame of latency if we're injecting a scroll
  // during the handling of a rAF aligned input event, such as mouse move.
  if (handling_input_state_) {
    InjectScrollGestureParams params{device, delta, granularity,
                                     scrollable_area_element_id, injected_type};
    handling_input_state_->injected_scroll_params.push_back(params);
  } else {
    base::TimeTicks now = base::TimeTicks::Now();
    std::unique_ptr<WebGestureEvent> gesture_event =
        WebGestureEvent::GenerateInjectedScrollGesture(
            injected_type, now, device, gfx::PointF(0, 0), delta, granularity);
    if (injected_type == WebInputEvent::Type::kGestureScrollBegin) {
      gesture_event->data.scroll_begin.scrollable_area_element_id =
          scrollable_area_element_id.GetStableId();
    }

    std::unique_ptr<WebCoalescedInputEvent> web_scoped_gesture_event =
        std::make_unique<WebCoalescedInputEvent>(std::move(gesture_event),
                                                 ui::LatencyInfo());
    widget_->client()->QueueSyntheticEvent(std::move(web_scoped_gesture_event));
  }
}

void WidgetBaseInputHandler::HandleInjectedScrollGestures(
    std::vector<InjectScrollGestureParams> injected_scroll_params,
    const WebInputEvent& input_event,
    const ui::LatencyInfo& original_latency_info) {
  DCHECK(injected_scroll_params.size());

  base::TimeTicks original_timestamp;
  bool found_original_component = original_latency_info.FindLatency(
      ui::INPUT_EVENT_LATENCY_ORIGINAL_COMPONENT, &original_timestamp);
  DCHECK(found_original_component);

  gfx::PointF position = PositionInWidgetFromInputEvent(input_event);
  for (const InjectScrollGestureParams& params : injected_scroll_params) {
    // Set up a new LatencyInfo for the injected scroll - this is the original
    // LatencyInfo for the input event that was being handled when the scroll
    // was injected. This new LatencyInfo will have a modified type, and an
    // additional scroll update component. Also set up a SwapPromiseMonitor that
    // will cause the LatencyInfo to be sent up with the compositor frame, if
    // the GSU causes a commit. This allows end to end latency to be logged for
    // the injected scroll, annotated with the correct type.
    ui::LatencyInfo scrollbar_latency_info(original_latency_info);

    // Currently only scrollbar is supported - if this DCHECK hits due to a
    // new type being injected, please modify the type passed to
    // |set_source_event_type()|.
    DCHECK(params.device == WebGestureDevice::kScrollbar);
    scrollbar_latency_info.set_source_event_type(
        ui::SourceEventType::SCROLLBAR);
    scrollbar_latency_info.AddLatencyNumber(
        ui::LatencyComponentType::INPUT_EVENT_LATENCY_RENDERER_MAIN_COMPONENT);

    if (params.type == WebInputEvent::Type::kGestureScrollUpdate) {
      if (input_event.GetType() != WebInputEvent::Type::kGestureScrollUpdate) {
        scrollbar_latency_info.AddLatencyNumberWithTimestamp(
            last_injected_gesture_was_begin_
                ? ui::INPUT_EVENT_LATENCY_FIRST_SCROLL_UPDATE_ORIGINAL_COMPONENT
                : ui::INPUT_EVENT_LATENCY_SCROLL_UPDATE_ORIGINAL_COMPONENT,
            original_timestamp);
      } else {
        // If we're injecting a GSU in response to a GSU (touch drags of the
        // scrollbar thumb in Blink handles GSUs, and reverses them with
        // injected GSUs), the LatencyInfo will already have the appropriate
        // SCROLL_UPDATE component set.
        DCHECK(
            scrollbar_latency_info.FindLatency(
                ui::INPUT_EVENT_LATENCY_FIRST_SCROLL_UPDATE_ORIGINAL_COMPONENT,
                nullptr) ||
            scrollbar_latency_info.FindLatency(
                ui::INPUT_EVENT_LATENCY_SCROLL_UPDATE_ORIGINAL_COMPONENT,
                nullptr));
      }
    }

    std::unique_ptr<WebGestureEvent> gesture_event =
        WebGestureEvent::GenerateInjectedScrollGesture(
            params.type, input_event.TimeStamp(), params.device, position,
            params.scroll_delta, params.granularity);
    if (params.type == WebInputEvent::Type::kGestureScrollBegin) {
      gesture_event->data.scroll_begin.scrollable_area_element_id =
          params.scrollable_area_element_id.GetStableId();
      last_injected_gesture_was_begin_ = true;
    } else {
      last_injected_gesture_was_begin_ = false;
    }

    {
      cc::LatencyInfoSwapPromiseMonitor swap_promise_monitor(
          &scrollbar_latency_info,
          widget_->LayerTreeHost()->GetSwapPromiseManager(), nullptr);
      auto scoped_event_metrics_monitor =
          widget_->LayerTreeHost()->GetScopedEventMetricsMonitor(
              cc::EventMetrics::Create(gesture_event->GetTypeAsUiEventType(),
                                       gesture_event->TimeStamp(),
                                       gesture_event->GetScrollInputType()));
      widget_->client()->HandleInputEvent(
          WebCoalescedInputEvent(*gesture_event, scrollbar_latency_info));
    }
  }
}

bool WidgetBaseInputHandler::DidChangeCursor(const ui::Cursor& cursor) {
  if (current_cursor_.has_value() && current_cursor_.value() == cursor)
    return false;
  current_cursor_ = cursor;
  return true;
}

bool WidgetBaseInputHandler::ProcessTouchAction(WebTouchAction touch_action) {
  if (!handling_input_state_)
    return false;
  // Ignore setTouchAction calls that result from synthetic touch events (eg.
  // when blink is emulating touch with mouse).
  if (!handling_input_state_->touch_start_or_move)
    return false;
  handling_input_state_->touch_action = touch_action;
  return true;
}

void WidgetBaseInputHandler::ShowVirtualKeyboard() {
#if defined(OS_ANDROID)
  if (handling_input_state_) {
    handling_input_state_->show_virtual_keyboard = true;
    return;
  }
#endif
  widget_->ShowVirtualKeyboard();
}

void WidgetBaseInputHandler::UpdateTextInputState() {
#if defined(OS_ANDROID)
  if (handling_input_state_)
    return;
#endif
  widget_->UpdateTextInputState();
}

}  // namespace blink
