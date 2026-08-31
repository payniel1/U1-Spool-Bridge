// Read a response body on stdin, print how the firmware classifies it.
#include <cstdio>
#include <string>
#include "u1_reply.h"
int main() {
  std::string body, line;
  char buf[4096];
  while (fgets(buf, sizeof buf, stdin)) body += buf;
  while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
  char msg[160];
  const char *n[] = {"OK", "BAD_FIELD", "ERROR", "UNKNOWN"};
  printf("%s\t%s\n", n[u1ClassifyReply(body.c_str(), msg, sizeof msg)], msg);
  return 0;
}
