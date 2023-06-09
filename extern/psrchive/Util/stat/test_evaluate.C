#include "Error.h"
#include "evaluate.h"

#include <string>
#include <iostream>

using namespace std;

int main () try
{
  string eval = "test {4.5 + 3.4} this";
  cerr << "eval='" << eval << "'" << endl;

  string result = evaluate (eval);
  cerr << "result='" << result << "'" << endl;

  if (result != "test 7.9 this") {
    cerr << "unexpected result = " << result << endl;
    return -1;
  }

  eval = "test {4.5678 + 3.4}%3";

  result = evaluate (eval);

  if (result != "test 7.97") {
    cerr << "unexpected result = " << result << endl;
    return -1;
  }

  eval = "test min{4.5678,3.4}";
  cerr << "eval='" << eval << "'" << endl;

  result = evaluate (eval);
  cerr << "result='" << result << "'" << endl;

  if (result != "test 3.4") {
    cerr << "unexpected result = " << result << endl;
    return -1;
  }

  cerr << "evaluate function passes all tests" << endl;
  return 0;

}
 catch (Error& error) {
   cerr << error << endl;
   return -1;
 }
