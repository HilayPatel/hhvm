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

#ifndef incl_HPHP_PAGELET_SERVER_H_
#define incl_HPHP_PAGELET_SERVER_H_

#include <string>
#include <atomic>
#include <set>
#include <deque>

#include "hphp/runtime/base/type-array.h"
#include "hphp/runtime/server/transport.h"
#include "hphp/runtime/server/server-task-event.h"
#include "hphp/util/synchronizable.h"

namespace HPHP {
///////////////////////////////////////////////////////////////////////////////

struct PageletTransport;
struct PageletServerTaskEvent;

struct PageletServer {
  static bool Enabled();
  static void Restart();
  static void Stop();

  /**
   * Create a task. This returns a task handle, or null object
   * if there are no worker threads.
   */
  static Resource TaskStart(
    const String& url, const Array& headers,
    const String& remote_host,
    const String& post_data = null_string,
    const Array& files = null_array,
    int timeoutSeconds = -1,
    PageletServerTaskEvent *event = nullptr
  );

  /**
   * Query if a task is finished. This is non-blocking and can be called as
   * many times as desired.
   */
  static int64_t TaskStatus(const Resource& task);

  /**
   * Get results of a task. This is blocking until task is finished or times
   * out. The status code is set to -1 in the event of a timeout.
   */
  static String TaskResult(const Resource& task,
                           Array &headers,
                           int &code,
                           int64_t timeout_ms);

  /**
   * Add a piece of response to the pipeline.
   */
  static void AddToPipeline(const std::string &s);

  /**
   * Check active threads and queued requests
   */
  static int GetActiveWorker();
  static int GetQueuedJobs();
};

struct PageletTransport final : Transport, Synchronizable {
  PageletTransport(
    const String& url, const Array& headers, const String& postData,
    const String& remoteHost,
    const std::set<std::string> &rfc1867UploadedFiles,
    const Array& files, int timeoutSeconds);

  /**
   * Implementing Transport...
   */
  const char *getUrl() override;
  const char *getRemoteHost() override;
  uint16_t getRemotePort() override;
  const void *getPostData(size_t &size) override;
  Method getMethod() override;
  std::string getHeader(const char *name) override;
  void getHeaders(HeaderMap &headers) override;
  void addHeaderImpl(const char *name, const char *value) override;
  void removeHeaderImpl(const char *name) override;
  void sendImpl(const void *data, int size, int code, bool chunked, bool eom)
       override;
  void onSendEndImpl() override;
  bool isUploadedFile(const String& filename) override;
  bool getFiles(std::string &files) override;

  // task interface
  bool isDone();

  void addToPipeline(const std::string &s);

  bool isPipelineEmpty();

  String getResults(
    Array &headers,
    int &code,
    int64_t timeout_ms
  );

  bool getResults(
    Array &results,
    int &code,
    PageletServerTaskEvent* next_event
  );

  // ref counting
  void incRefCount();
  void decRefCount();

  const timespec& getStartTimer() const;
  int getTimeoutSeconds() const;

  void setAsioEvent(PageletServerTaskEvent *event);

private:
  std::atomic<int> m_refCount;
  int m_timeoutSeconds;

  std::string m_url;
  HeaderMap m_requestHeaders;
  bool m_get;
  std::string m_postData;
  std::string m_remoteHost;

  bool m_done;
  HeaderMap m_responseHeaders;
  std::string m_response;
  int m_code;

  std::deque<std::string> m_pipeline; // the intermediate pagelet results
  std::set<std::string> m_rfc1867UploadedFiles;
  std::string m_files; // serialized to use as $_FILES

  // points to an event with an attached waithandle from a different request
  PageletServerTaskEvent *m_event;
};

struct PageletServerTaskEvent final : AsioExternalThreadEvent {

  ~PageletServerTaskEvent() {
    if (m_job) m_job->decRefCount();
  }

  void finish() {
    markAsFinished();
  }

  void setJob(PageletTransport *job) {
    job->incRefCount();
    m_job = job;
  }

protected:

  void unserialize(Cell& result) override final {
    // Main string responses from pagelet thread.
    Array responses = Array::Create();

    // Create an event for the next results that might be used.
    PageletServerTaskEvent *event = new PageletServerTaskEvent();

    int code = 0;
    // Fetch all results from the transport that are currently available.
    bool done = m_job->getResults(responses, code, event);

    // Returned tuple/array.
    Array ret = Array::Create();
    ret.append(responses);

    if (done) {
      // If the whole thing is done, then we don't need a next event.
      event->abandon();
      ret.append(init_null_variant);
    } else {
      // The event was added to the job to be triggered next.
      ret.append(Variant{event->getWaitHandle()});
    }
    ret.append(Variant{code});

    cellDup(*(Variant(ret)).asCell(), result);
  }

private:
  PageletTransport* m_job;
  // string m_response;
  // Object m_next_wait_handle;
};

///////////////////////////////////////////////////////////////////////////////
}

#endif // incl_HPHP_PAGELET_SERVER_H_
