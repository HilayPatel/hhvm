/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-present Facebook, Inc. (http://www.facebook.com)  |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#include "hphp/runtime/server/warmup-request-handler.h"

#include "hphp/runtime/base/memory-manager.h"
#include "hphp/runtime/base/program-functions.h"

#include <folly/Memory.h>

using folly::make_unique;

namespace HPHP {
///////////////////////////////////////////////////////////////////////////////

void WarmupRequestHandler::setupRequest(Transport* transport) {
  m_reqHandler.setupRequest(transport);
}

void WarmupRequestHandler::teardownRequest(Transport* transport) noexcept {
  m_reqHandler.teardownRequest(transport);
}

void WarmupRequestHandler::handleRequest(Transport *transport) {
  // There is one WarmupRequestHandler per-thread, but we want to track request
  // count across all threads.  Therefore we let WarmupRequestHandlerFactory
  // track the global request count.
  m_factory->bumpReqCount();
  m_reqHandler.handleRequest(transport);
}

void WarmupRequestHandler::abortRequest(Transport *transport) {
  m_reqHandler.abortRequest(transport);
}

void WarmupRequestHandler::logToAccessLog(Transport *transport) {
  m_reqHandler.logToAccessLog(transport);
}

std::unique_ptr<RequestHandler> WarmupRequestHandlerFactory::createHandler() {
  return
    folly::make_unique<WarmupRequestHandler>(m_timeout, shared_from_this());
}

void WarmupRequestHandlerFactory::bumpReqCount() {
  // Bump the request count.  When we hit m_warmupReqThreshold,
  // add additional threads to the server.
  auto const oldReqNum = m_reqNumber.fetch_add(1, std::memory_order_relaxed);
  if (oldReqNum != m_warmupReqThreshold) {
    return;
  }

  auto const num = m_additionalThreads.load();
  if (!num) {
    return;
  }

  Logger::Info("Finished warmup; adding %d new worker threads", num);
  m_server->addWorkers(num);

  // Set to zero so we can't do it if the req counter wraps.
  m_additionalThreads.store(0);
}

///////////////////////////////////////////////////////////////////////////////
}
