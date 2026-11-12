#include "cat/core.hpp"
int main() { bool ok=cat::self_test(); cat::log(ok?"info":"error","self-test",ok?"PASS":"FAIL"); return ok?0:1; }
