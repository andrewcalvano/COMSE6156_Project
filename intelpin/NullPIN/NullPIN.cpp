#include <stdio.h>
#include "pin.H"

int main(INT32 argc, CHAR **argv)
{
    PIN_Init(argc, argv);
    //INS_AddInstrumentFunction(Instruction, 0);
    
    // Never returns
    PIN_StartProgram();
    
    return 0;
}
