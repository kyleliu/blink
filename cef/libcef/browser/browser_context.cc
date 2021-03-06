// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/browser/browser_context.h"
#include "libcef/browser/content_browser_client.h"
#include "libcef/browser/thread_util.h"

#include "base/logging.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"

#if DCHECK_IS_ON()
base::AtomicRefCount CefBrowserContext::DebugObjCt = 0;
#endif

CefBrowserContext::CefBrowserContext(bool is_proxy)
    : is_proxy_(is_proxy) {
#if DCHECK_IS_ON()
  base::AtomicRefCountInc(&DebugObjCt);
#endif
}

CefBrowserContext::~CefBrowserContext() {
  // Should be cleared in Shutdown().
  DCHECK(!resource_context_.get());

#if DCHECK_IS_ON()
  base::AtomicRefCountDec(&DebugObjCt);
#endif
}

void CefBrowserContext::Initialize() {
  content::BrowserContext::Initialize(this, GetPath());

  resource_context_.reset(new CefResourceContext(
      IsOffTheRecord(),
      GetHandler()));

  BrowserContextDependencyManager::GetInstance()->CreateBrowserContextServices(
      this);

  // Spell checking support and possibly other subsystems retrieve the
  // PrefService associated with a BrowserContext via UserPrefs::Get().
  PrefService* pref_service = GetPrefs();
  DCHECK(pref_service);
  user_prefs::UserPrefs::Set(this, pref_service);
}

void CefBrowserContext::Shutdown() {
  CEF_REQUIRE_UIT();

  // Send notifications to clean up objects associated with this Profile.
  // MaybeSendDestroyedNotification();

  // Remove any BrowserContextKeyedServiceFactory associations. This must be
  // called before the ProxyService owned by CefBrowserContextImpl is destroyed.
  BrowserContextDependencyManager::GetInstance()->DestroyBrowserContextServices(
      this);

  if (!is_proxy_) {
    // Shuts down the storage partitions associated with this browser context.
    // This must be called before the browser context is actually destroyed
    // and before a clean-up task for its corresponding IO thread residents
    // (e.g. ResourceContext) is posted, so that the classes that hung on
    // StoragePartition can have time to do necessary cleanups on IO thread.
    ShutdownStoragePartitions();
  }

  if (resource_context_.get()) {
    // Destruction of the ResourceContext will trigger destruction of all
    // associated URLRequests.
    content::BrowserThread::DeleteSoon(
        content::BrowserThread::IO, FROM_HERE, resource_context_.release());
  }
}

content::ResourceContext* CefBrowserContext::GetResourceContext() {
  return resource_context_.get();
}

net::URLRequestContextGetter* CefBrowserContext::GetRequestContext() {
  CEF_REQUIRE_UIT();
  return GetDefaultStoragePartition(this)->GetURLRequestContext();
}

net::URLRequestContextGetter* CefBrowserContext::CreateMediaRequestContext() {
  return GetRequestContext();
}

net::URLRequestContextGetter*
    CefBrowserContext::CreateMediaRequestContextForStoragePartition(
    const base::FilePath& partition_path,
    bool in_memory) {
  return nullptr;
}

void CefBrowserContext::OnRenderFrameDeleted(int render_process_id,
                                             int render_frame_id,
                                             bool is_main_frame,
                                             bool is_guest_view) {
  CEF_POST_TASK(CEF_IOT,
      base::Bind(&CefBrowserContext::RenderFrameDeletedOnIOThread, this,
                 render_process_id, render_frame_id, is_main_frame,
                 is_guest_view));
}

void CefBrowserContext::OnPurgePluginListCache() {
  CEF_POST_TASK(CEF_IOT,
      base::Bind(&CefBrowserContext::PurgePluginListCacheOnIOThread, this));
}

void CefBrowserContext::RenderFrameDeletedOnIOThread(int render_process_id,
                                                     int render_frame_id,
                                                     bool is_main_frame,
                                                     bool is_guest_view) {
  if (resource_context_ && is_main_frame) {
    DCHECK_GE(render_process_id, 0);
    resource_context_->ClearPluginLoadDecision(render_process_id);
  }
}

void CefBrowserContext::PurgePluginListCacheOnIOThread() {
  if (resource_context_)
    resource_context_->ClearPluginLoadDecision(-1);
}
