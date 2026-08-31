// Just enough Arduino to type-check the networking translation units on a host.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
class String {
  std::string s;
 public:
  String() {}
  String(const char *p) : s(p ? p : "") {}
  String(const std::string &o) : s(o) {}
  explicit String(int v) { char b[24]; snprintf(b, sizeof b, "%d", v); s = b; }
  explicit String(unsigned v) { char b[24]; snprintf(b, sizeof b, "%u", v); s = b; }
  const char *c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
  bool isEmpty() const { return s.empty(); }
  int indexOf(const char *n) const { auto p = s.find(n); return p == std::string::npos ? -1 : (int)p; }
  String &operator+=(const String &o) { s += o.s; return *this; }
  String &operator+=(const char *o) { s += o; return *this; }
  friend String operator+(const String &a, const String &b) { return String(a.s + b.s); }
  friend String operator+(const String &a, const char *b) { return String(a.s + b); }
  friend String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }
  operator const char *() const { return s.c_str(); }
  // What ArduinoJson's ::String specialisation calls.
  bool concat(const char *p, size_t n) { s.append(p, n); return true; }
  bool concat(const char *p) { s += p; return true; }
  void reserve(size_t n) { s.reserve(n); }
  char operator[](size_t i) const { return i < s.size() ? s[i] : 0; }
  void   trim() { auto b=s.find_first_not_of(" \t\r\n"); auto e=s.find_last_not_of(" \t\r\n");
                  s = (b==std::string::npos) ? "" : s.substr(b, e-b+1); }
  bool   startsWith(const char *p) const { return s.rfind(p,0)==0; }
  bool   endsWith(const char *p) const { size_t n=strlen(p); return s.size()>=n && s.compare(s.size()-n,n,p)==0; }
  String substring(size_t a) const { return String(a<s.size()? s.substr(a): std::string()); }
  String substring(size_t a, size_t b) const { return String(a<s.size()? s.substr(a, b-a): std::string()); }
  int    lastIndexOf(char c) const { auto p=s.rfind(c); return p==std::string::npos?-1:(int)p; }
  int    indexOf(char c) const { auto p=s.find(c); return p==std::string::npos?-1:(int)p; }
  long   toInt() const { return strtol(s.c_str(), nullptr, 10); }
  void   remove(size_t i) { if (i<s.size()) s.erase(i); }
  bool   operator==(const char *o) const { return s == (o?o:""); }
  bool   operator!=(const char *o) const { return !(*this==o); }
};
#include "esp_stub.h"
template <class T, class L, class H> constexpr T constrain(T v, L lo, H hi) {
  return v < (T)lo ? (T)lo : (v > (T)hi ? (T)hi : v);
}
static inline unsigned long millis() { return 0; }
static inline void delay(unsigned long) {}
