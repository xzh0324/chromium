// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/url_request_mock_http_job.h"

#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/string_util.h"
#include "net/base/net_util.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request_filter.h"

static const char kMockHostname[] = "mock.http";
static const wchar_t kMockHeaderFileSuffix[] = L".mock-http-headers";

std::wstring URLRequestMockHTTPJob::base_path_ = L"";

/* static */
URLRequestJob* URLRequestMockHTTPJob::Factory(URLRequest* request,
                                              const std::string& scheme) {
  return new URLRequestMockHTTPJob(request,
                                   GetOnDiskPath(base_path_, request, scheme));
}

/* static */
void URLRequestMockHTTPJob::AddUrlHandler(const std::wstring& base_path) {
  base_path_ = base_path;

  // Add kMockHostname to URLRequestFilter.
  URLRequestFilter* filter = URLRequestFilter::GetInstance();
  filter->AddHostnameHandler("http", kMockHostname,
                             URLRequestMockHTTPJob::Factory);
}

/* static */
GURL URLRequestMockHTTPJob::GetMockUrl(const std::wstring& path) {
  std::string url = "http://";
  url.append(kMockHostname);
  url.append("/");
  url.append(WideToUTF8(path));
  return GURL(url);
}

/* static */
FilePath URLRequestMockHTTPJob::GetOnDiskPath(const std::wstring& base_path,
                                              URLRequest* request,
                                              const std::string& scheme) {
  std::string file_url("file:///");
  file_url += WideToUTF8(base_path);
  file_url += request->url().path();

  // Convert the file:/// URL to a path on disk.
  FilePath file_path;
  net::FileURLToFilePath(GURL(file_url), &file_path);
  return file_path;
}

URLRequestMockHTTPJob::URLRequestMockHTTPJob(URLRequest* request,
                                             const FilePath& file_path)
    : URLRequestFileJob(request, file_path) { }

// Public virtual version.
void URLRequestMockHTTPJob::GetResponseInfo(net::HttpResponseInfo* info) {
  // Forward to private const version.
  GetResponseInfoConst(info);
}

bool URLRequestMockHTTPJob::IsRedirectResponse(GURL* location,
                                               int* http_status_code) {
  // Override the URLRequestFileJob implementation to invoke the default one
  // based on HttpResponseInfo.
  return URLRequestJob::IsRedirectResponse(location, http_status_code);
}

// Private const version.
void URLRequestMockHTTPJob::GetResponseInfoConst(
    net::HttpResponseInfo* info) const {
  std::wstring header_file = file_path_.ToWStringHack() + kMockHeaderFileSuffix;
  std::string raw_headers;
  if (!file_util::ReadFileToString(header_file, &raw_headers))
    return;

  // ParseRawHeaders expects \0 to end each header line.
  ReplaceSubstringsAfterOffset(&raw_headers, 0, "\n", std::string("\0", 1));
  info->headers = new net::HttpResponseHeaders(raw_headers);
}

bool URLRequestMockHTTPJob::GetMimeType(std::string* mime_type) const {
  net::HttpResponseInfo info;
  GetResponseInfoConst(&info);
  return info.headers && info.headers->GetMimeType(mime_type);
}

bool URLRequestMockHTTPJob::GetCharset(std::string* charset) {
  net::HttpResponseInfo info;
  GetResponseInfo(&info);
  return info.headers && info.headers->GetCharset(charset);
}
