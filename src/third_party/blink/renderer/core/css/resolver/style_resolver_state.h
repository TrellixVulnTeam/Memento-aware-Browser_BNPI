/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc.
 * All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_STYLE_RESOLVER_STATE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_STYLE_RESOLVER_STATE_H_

#include <memory>
#include "base/macros.h"
#include "third_party/blink/renderer/core/animation/css/css_animation_update.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_pending_substitution_value.h"
#include "third_party/blink/renderer/core/css/css_property_name.h"
#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/css/css_to_length_conversion_data.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_mode.h"
#include "third_party/blink/renderer/core/css/pseudo_style_request.h"
#include "third_party/blink/renderer/core/css/resolver/css_to_style_map.h"
#include "third_party/blink/renderer/core/css/resolver/element_resolve_context.h"
#include "third_party/blink/renderer/core/css/resolver/element_style_resources.h"
#include "third_party/blink/renderer/core/css/resolver/font_builder.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/style/cached_ua_style.h"

namespace blink {

class ComputedStyle;
class FontDescription;
class PseudoElement;

// A per-element object which wraps an ElementResolveContext. It collects state
// throughout the process of computing the style. It also gives convenient
// access to other element-related information.
class CORE_EXPORT StyleResolverState {
  STACK_ALLOCATED();

 public:
  StyleResolverState(Document&,
                     Element&,
                     const ComputedStyle* parent_style = nullptr,
                     const ComputedStyle* layout_parent_style = nullptr);
  StyleResolverState(Document&,
                     Element&,
                     PseudoId,
                     PseudoElementStyleRequest::RequestType,
                     const ComputedStyle* parent_style,
                     const ComputedStyle* layout_parent_style);
  ~StyleResolverState();

  // In FontFaceSet and CanvasRenderingContext2D, we don't have an element to
  // grab the document from.  This is why we have to store the document
  // separately.
  Document& GetDocument() const { return *document_; }
  // These are all just pass-through methods to ElementResolveContext.
  Element& GetElement() const { return element_context_.GetElement(); }
  TreeScope& GetTreeScope() const;
  const ContainerNode* ParentNode() const {
    return element_context_.ParentNode();
  }
  const ComputedStyle* RootElementStyle() const {
    return element_context_.RootElementStyle();
  }
  EInsideLink ElementLinkState() const {
    return element_context_.ElementLinkState();
  }
  bool DistributedToV0InsertionPoint() const {
    return element_context_.DistributedToV0InsertionPoint();
  }

  const ElementResolveContext& ElementContext() const {
    return element_context_;
  }

  void SetStyle(scoped_refptr<ComputedStyle>);
  const ComputedStyle* Style() const { return style_.get(); }
  ComputedStyle* Style() { return style_.get(); }
  ComputedStyle& StyleRef() {
    DCHECK(style_);
    return *style_;
  }
  scoped_refptr<ComputedStyle> TakeStyle();

  const CSSToLengthConversionData& CssToLengthConversionData() const {
    return css_to_length_conversion_data_;
  }
  CSSToLengthConversionData FontSizeConversionData() const;
  CSSToLengthConversionData UnzoomedLengthConversionData() const;

  void SetConversionFontSizes(
      const CSSToLengthConversionData::FontSizes& font_sizes) {
    css_to_length_conversion_data_.SetFontSizes(font_sizes);
  }
  void SetConversionZoom(float zoom) {
    css_to_length_conversion_data_.SetZoom(zoom);
  }

  CSSAnimationUpdate& AnimationUpdate() { return animation_update_; }
  const CSSAnimationUpdate& AnimationUpdate() const {
    return animation_update_;
  }

  bool IsAnimationInterpolationMapReady() const {
    return is_animation_interpolation_map_ready_;
  }
  void SetIsAnimationInterpolationMapReady() {
    is_animation_interpolation_map_ready_ = true;
  }

  bool IsAnimatingCustomProperties() const {
    return is_animating_custom_properties_;
  }
  void SetIsAnimatingCustomProperties(bool value) {
    is_animating_custom_properties_ = value;
  }
  bool IsAnimatingRevert() const { return is_animating_revert_; }
  void SetIsAnimatingRevert(bool value) { is_animating_revert_ = value; }

  // Normally, we apply all active animation effects on top of the style created
  // by regular CSS declarations. However, !important declarations have a
  // higher priority than animation effects [1]. If we're currently animating
  // (not transitioning) a property which was declared !important in the base
  // style, this flag is set such that we can disable the base computed style
  // optimization.
  //
  // [1] https://drafts.csswg.org/css-cascade-4/#cascade-origin
  bool HasImportantOverrides() const { return has_important_overrides_; }
  void SetHasImportantOverrides() { has_important_overrides_ = true; }

  // This flag is set when applying an animation (or transition) for a font
  // affecting property. When such properties are animated, font-relative
  // units (e.g. em, ex) in the base style must respond to the animation.
  // Therefore we can't use the base computed style optimization in such cases.
  bool HasFontAffectingAnimation() const {
    return has_font_affecting_animation_;
  }
  void SetHasFontAffectingAnimation() { has_font_affecting_animation_ = true; }

  const Element* GetAnimatingElement() const;

  void SetParentStyle(scoped_refptr<const ComputedStyle>);
  const ComputedStyle* ParentStyle() const { return parent_style_.get(); }

  void SetLayoutParentStyle(scoped_refptr<const ComputedStyle>);
  const ComputedStyle* LayoutParentStyle() const {
    return layout_parent_style_.get();
  }

  void CacheUserAgentBorderAndBackground();

  const CachedUAStyle* GetCachedUAStyle() const {
    return cached_ua_style_.get();
  }

  ElementStyleResources& GetElementStyleResources() {
    return element_style_resources_;
  }

  void LoadPendingResources();

  // FIXME: Once styleImage can be made to not take a StyleResolverState
  // this convenience function should be removed. As-is, without this, call
  // sites are extremely verbose.
  StyleImage* GetStyleImage(CSSPropertyID property_id, const CSSValue& value) {
    return element_style_resources_.GetStyleImage(property_id, value);
  }

  FontBuilder& GetFontBuilder() { return font_builder_; }
  const FontBuilder& GetFontBuilder() const { return font_builder_; }
  // FIXME: These exist as a primitive way to track mutations to font-related
  // properties on a ComputedStyle. As designed, these are very error-prone, as
  // some callers set these directly on the ComputedStyle w/o telling us.
  // Presumably we'll want to design a better wrapper around ComputedStyle for
  // tracking these mutations and separate it from StyleResolverState.
  const FontDescription& ParentFontDescription() const;

  void SetZoom(float);
  void SetEffectiveZoom(float);
  void SetWritingMode(WritingMode);
  void SetTextOrientation(ETextOrientation);

  void SetHasDirAutoAttribute(bool value) { has_dir_auto_attribute_ = value; }
  bool HasDirAutoAttribute() const { return has_dir_auto_attribute_; }

  const CSSValue* GetCascadedColorValue() const {
    return cascaded_color_value_;
  }
  const CSSValue* GetCascadedVisitedColorValue() const {
    return cascaded_visited_color_value_;
  }

  void SetCascadedColorValue(const CSSValue* color) {
    cascaded_color_value_ = color;
  }
  void SetCascadedVisitedColorValue(const CSSValue* color) {
    cascaded_visited_color_value_ = color;
  }

  HeapHashMap<CSSPropertyID, Member<const CSSValue>>&
  ParsedPropertiesForPendingSubstitutionCache(
      const cssvalue::CSSPendingSubstitutionValue&) const;

  CSSParserMode GetParserMode() const;

  // If the input CSSValue is a CSSLightDarkValuePair, return the light or dark
  // CSSValue based on the UsedColorScheme. For all other values, just return a
  // reference to the passed value. If the property is a non-inherited one, mark
  // the ComputedStyle as having such a pair since that will make sure its not
  // stored in the MatchedPropertiesCache.
  const CSSValue& ResolveLightDarkPair(const CSSProperty&, const CSSValue&);

  // The dependencies we track here end up in an entry in the
  // MatchedPropertiesCache. Declarations such as "all:inherit" incurs several
  // hundred dependencies, which is too big to cache, hence the number of
  // dependencies we can track is limited.
  static const size_t kMaxDependencies = 8;

  // Mark the ComputedStyle as possibly dependent on the specified property.
  //
  // A "dependency" in this context means that one or more of the computed
  // values held by the ComputedStyle depends on the computed value of the
  // parent ComputedStyle.
  //
  // For example, a declaration such as background-color:var(--x) would incur
  // a dependency on --x.
  void MarkDependency(const CSSProperty&);

  // Returns the set of all properties seen by MarkDependency.
  //
  // The caller must check if the dependencies are valid via
  // HasValidDependencies() before calling this function.
  //
  // Note that this set might be larger than the actual set of dependencies,
  // as we do some degree of over-marking to keep the implementation simple.
  //
  // For example, we mark all custom properties referenced as dependencies, even
  // though the ComputedStyle itself may define a value for some or all of those
  // custom properties. In the following example, both --x and --y will be
  // added to this set, even though only --y is a true dependency:
  //
  //  div {
  //    --x: 10px;
  //    margin: var(--x) (--y);
  //  }
  //
  const HashSet<CSSPropertyName>& Dependencies() const {
    DCHECK(HasValidDependencies());
    return dependencies_;
  }

  // True if there's a dependency without the kComputedValueComparable flag.
  bool HasIncomparableDependency() const {
    return has_incomparable_dependency_;
  }

  bool HasValidDependencies() const {
    return dependencies_.size() <= kMaxDependencies;
  }

 private:
  enum class AnimatingElementType { kElement, kPseudoElement };

  StyleResolverState(Document&,
                     Element&,
                     PseudoElement*,
                     PseudoElementStyleRequest::RequestType,
                     AnimatingElementType,
                     const ComputedStyle* parent_style,
                     const ComputedStyle* layout_parent_style);

  CSSToLengthConversionData UnzoomedLengthConversionData(
      const ComputedStyle* font_style) const;

  ElementResolveContext element_context_;
  Document* document_;

  // style_ is the primary output for each element's style resolve.
  scoped_refptr<ComputedStyle> style_;

  CSSToLengthConversionData css_to_length_conversion_data_;

  // parent_style_ is not always just ElementResolveContext::ParentStyle(),
  // so we keep it separate.
  scoped_refptr<const ComputedStyle> parent_style_;
  // This will almost-always be the same that parent_style_, except in the
  // presence of display: contents. This is the style against which we have to
  // do adjustment.
  scoped_refptr<const ComputedStyle> layout_parent_style_;

  CSSAnimationUpdate animation_update_;
  bool is_animation_interpolation_map_ready_ = false;
  bool is_animating_custom_properties_ = false;
  // We can't use the base computed style optimization when 'revert' appears
  // in a keyframe. (We need to build the cascade to know what to revert to).
  // TODO(crbug.com/1068515): Refactor caching to remove these flags.
  bool is_animating_revert_ = false;
  bool has_important_overrides_ = false;
  bool has_font_affecting_animation_ = false;
  bool has_dir_auto_attribute_ = false;
  PseudoElementStyleRequest::RequestType pseudo_request_type_;

  const CSSValue* cascaded_color_value_ = nullptr;
  const CSSValue* cascaded_visited_color_value_ = nullptr;

  FontBuilder font_builder_;

  std::unique_ptr<CachedUAStyle> cached_ua_style_;

  ElementStyleResources element_style_resources_;
  Element* pseudo_element_;
  AnimatingElementType animating_element_type_;

  // Properties depended on by the ComputedStyle. This is known after the
  // cascade is applied.
  HashSet<CSSPropertyName> dependencies_;
  // True if there's an entry in 'dependencies_' which does not have the
  // CSSProperty::kComputedValueComparable flag set.
  bool has_incomparable_dependency_ = false;

  mutable HeapHashMap<
      Member<const cssvalue::CSSPendingSubstitutionValue>,
      Member<HeapHashMap<CSSPropertyID, Member<const CSSValue>>>>
      parsed_properties_for_pending_substitution_cache_;
  DISALLOW_COPY_AND_ASSIGN(StyleResolverState);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_STYLE_RESOLVER_STATE_H_
