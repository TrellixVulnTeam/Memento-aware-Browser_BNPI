// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/public/test/js_test_storage_util.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#import "base/test/ios/wait_util.h"
#include "base/values.h"
#include "ios/web/public/js_messaging/web_frame.h"
#import "ios/web/public/web_state.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using base::test::ios::kWaitForJSCompletionTimeout;
using base::test::ios::WaitUntilConditionOrTimeout;

namespace {

// Convenience wrapper for web_frame.CallJavaScriptFunction that synchronously
// calls the provided function.
bool ExecuteJavaScriptInFrame(
    web::WebFrame* web_frame,
    const std::string& name,
    const std::vector<base::Value>& parameters,
    base::OnceCallback<void(const base::Value*)> callback,
    base::TimeDelta timeout) {
  __block bool completed = false;
  __block base::OnceCallback<void(const base::Value*)> block_callback =
      std::move(callback);
  web_frame->CallJavaScriptFunction(name, parameters,
                                    base::BindOnce(^(const base::Value* value) {
                                      completed = true;
                                      std::move(block_callback).Run(value);
                                    }),
                                    timeout);
  bool success =
      WaitUntilConditionOrTimeout(kWaitForJSCompletionTimeout, ^bool {
        return completed;
      });
  return success;
}

// Convenience wrapper for web_frame.CallJavaScriptFunction that synchronously
// calls the provided function.
bool ExecuteJavaScriptInFrame(web::WebFrame* web_frame,
                              const std::string& name,
                              const std::vector<base::Value>& parameters) {
  return ExecuteJavaScriptInFrame(
      web_frame, name, parameters,
      base::BindOnce(^(const base::Value*){
      }),
      base::TimeDelta::FromSeconds(kWaitForJSCompletionTimeout));
}

// Saves |key|, |value| to a Javascript storage type in |web_frame| using the
// __gCrWeb function |name|. Places any error message from the JavaScript into
// |error_message|.
bool SetStorage(web::WebFrame* web_frame,
                const std::string& set_function,
                NSString* key,
                NSString* value,
                NSString** error_message) {
  __block NSString* block_error_message;
  __block bool set_success = false;
  std::vector<base::Value> params;
  params.push_back(base::Value(base::SysNSStringToUTF8(key)));
  params.push_back(base::Value(base::SysNSStringToUTF8(value)));
  bool success = ExecuteJavaScriptInFrame(
      web_frame, set_function, params,
      base::BindOnce(^(const base::Value* value) {
        if (value->is_bool()) {
          set_success = value->GetBool();
        } else if (value->is_dict()) {
          block_error_message =
              base::SysUTF8ToNSString(value->FindPath("message")->GetString());
          set_success = true;
        }
      }),
      base::TimeDelta::FromSeconds(kWaitForJSCompletionTimeout));
  if (error_message) {
    *error_message = block_error_message;
  }

  return success && set_success;
}

// Reads the value for the given |key| from storage on |web_frame| using
// the __gCrWeb function |name|. The read value will be placed in |result| and
// any JavaScript error will be placed in |error_message|.
bool GetStorage(web::WebFrame* web_frame,
                const std::string& get_function,
                NSString* key,
                NSString** result,
                NSString** error_message) {
  __block NSString* block_result;
  __block NSString* block_error_message;
  __block bool lookup_success = false;
  std::vector<base::Value> params;
  params.push_back(base::Value(base::SysNSStringToUTF8(key)));
  bool success = ExecuteJavaScriptInFrame(
      web_frame, get_function, params,
      base::BindOnce(^(const base::Value* value) {
        if (value->is_string()) {
          block_result = base::SysUTF8ToNSString(value->GetString());
          lookup_success = true;
        } else if (value->is_dict()) {
          block_error_message =
              base::SysUTF8ToNSString(value->FindPath("message")->GetString());
          lookup_success = true;
        } else {
          lookup_success = false;
        }
      }),
      base::TimeDelta::FromSeconds(kWaitForJSCompletionTimeout));

  if (error_message) {
    *error_message = block_error_message;
  }
  if (result) {
    *result = block_result;
  }
  return success && lookup_success;
}

}  // namespace

namespace web {
namespace test {

bool SetCookie(WebFrame* web_frame, NSString* key, NSString* value) {
  std::vector<base::Value> params;
  NSString* cookie = [NSString
      stringWithFormat:@"%@=%@; Expires=Tue, 05-May-9999 02:18:23 GMT; Path=/",
                       key, value];
  params.push_back(base::Value(base::SysNSStringToUTF8(cookie)));
  return ExecuteJavaScriptInFrame(web_frame, "cookieTest.setCookie", params);
}

bool GetCookies(WebFrame* web_frame, NSString** cookies) {
  __block NSString* result = nil;
  std::vector<base::Value> params;
  bool success = ExecuteJavaScriptInFrame(
      web_frame, "cookieTest.getCookies", params,
      base::BindOnce(^(const base::Value* value) {
        ASSERT_TRUE(value->is_string());
        result = base::SysUTF8ToNSString(value->GetString());
      }),
      base::TimeDelta::FromSeconds(kWaitForJSCompletionTimeout));
  if (cookies) {
    *cookies = result;
  }
  return success;
}

bool SetLocalStorage(WebFrame* web_frame,
                     NSString* key,
                     NSString* value,
                     NSString** error_message) {
  return SetStorage(web_frame, "cookieTest.setLocalStorage", key, value,
                    error_message);
}

bool GetLocalStorage(WebFrame* web_frame,
                     NSString* key,
                     NSString** result,
                     NSString** error_message) {
  return GetStorage(web_frame, "cookieTest.getLocalStorage", key, result,
                    error_message);
}

bool SetSessionStorage(WebFrame* web_frame,
                       NSString* key,
                       NSString* value,
                       NSString** error_message) {
  return SetStorage(web_frame, "cookieTest.setSessionStorage", key, value,
                    error_message);
}

bool GetSessionStorage(WebFrame* web_frame,
                       NSString* key,
                       NSString** result,
                       NSString** error_message) {
  return GetStorage(web_frame, "cookieTest.getSessionStorage", key, result,
                    error_message);
}

bool SetCache(WebFrame* web_frame,
              WebState* web_state,
              NSString* key,
              NSString* value,
              NSString** error_message) {
  // Cache is an async api. The test injected javascript will send a message
  // when the async is done, so listen for that here.
  __block bool async_success = false;
  __block NSString* block_error_message;
  std::unique_ptr<WebState::ScriptCommandSubscription> subscription_ =
      web_state->AddScriptCommandCallback(
          base::BindRepeating(^(const base::DictionaryValue& message,
                                const GURL& page_url, bool user_is_interacting,
                                web::WebFrame* sender_frame) {
            const base::Value* result = message.FindPath("result");
            if (!result) {
              return;
            }
            if (result->is_bool()) {
              async_success = result->GetBool();
            } else {
              block_error_message = base::SysUTF8ToNSString(
                  result->FindPath("message")->GetString());
              async_success = true;
            }
          }),
          "cookieTest");

  if (!SetStorage(web_frame, "cookieTest.setCache", key, value, nil)) {
    return false;
  }

  bool success =
      WaitUntilConditionOrTimeout(kWaitForJSCompletionTimeout, ^bool {
        return async_success;
      });

  if (error_message) {
    *error_message = block_error_message;
  }

  return success;
}

bool GetCache(WebFrame* web_frame,
              WebState* web_state,
              NSString* key,
              NSString** result,
              NSString** error_message) {
  // Cache is an async api. The test injected javascript will send a message
  // when the async is done, so listen for that here.
  __block bool async_success = false;
  __block NSString* block_result;
  __block NSString* block_error_message;
  std::unique_ptr<WebState::ScriptCommandSubscription> subscription_ =
      web_state->AddScriptCommandCallback(
          base::BindRepeating(^(const base::DictionaryValue& message,
                                const GURL& page_url, bool user_is_interacting,
                                web::WebFrame* sender_frame) {
            const base::Value* javascript_result = message.FindPath("result");
            if (!javascript_result) {
              return;
            }
            if (javascript_result->is_string()) {
              block_result =
                  base::SysUTF8ToNSString(javascript_result->GetString());
              async_success = true;
            } else if (javascript_result->is_dict()) {
              block_error_message = base::SysUTF8ToNSString(
                  javascript_result->FindPath("message")->GetString());
              async_success = true;
            } else {
              async_success = false;
            }
          }),
          "cookieTest");

  if (!GetStorage(web_frame, "cookieTest.getCache", key, nil, nil)) {
    return false;
  }

  bool success =
      WaitUntilConditionOrTimeout(kWaitForJSCompletionTimeout, ^bool {
        return async_success;
      });

  if (result) {
    *result = block_result;
  }
  if (error_message) {
    *error_message = block_error_message;
  }

  return success;
}

}  // namespace test
}  // namespace web
