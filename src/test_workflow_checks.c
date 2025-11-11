/***
    Test file to demonstrate workflow check improvements
    This file has deliberate formatting and whitespace issues
***/

#include <stdio.h>

// Deliberate formatting issues for cpp-linter to catch
int badly_formatted_function(int a,int b,int c) {
    if(a>b){return a;}else{return b;}
}

// Line below has trailing whitespace    
void function_with_trailing_space() {
    int x=1+2+3;  
    printf("test");
}

// Mixed spacing issues
int another_test( int param ){
	// Tab before this comment
  return param*2;
}
