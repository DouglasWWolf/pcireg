//=================================================================================================
// pcireg - Tool for command-line read/write of PCI device registers
//
// Author: D. Wolf
//=================================================================================================
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdexcept>
#include "PciDevice.h"

using namespace std;


const int OM_NONE = 0;
const int OM_DEC  = 1;
const int OM_HEX  = 2;
const int OM_BOTH = 3;    
int output_mode = OM_NONE;


int       pciRegion   = -1;
bool      isAxiWrite  = false;
uint32_t  axiAddr = 0xFFFFFFFF;
uint32_t  axiData;
string    device;
int       vendorID;
int       deviceID;
PciDevice PCI;


void showHelp();
void parseCommandLine(const char** argv);
void execute();



//=================================================================================================
// main() - Command line is program_name [-r <region#>] <address> [data]
//=================================================================================================
int main(int argc, const char** argv)
{
    char* p;

    parseCommandLine(argv); 

    // If there was no device specified on the command line, try fetching it from
    // the environment variable
    if (device == "") 
    {
        p = getenv("pcireg_device");
        if (p) device = p;
    }

    // If no device is otherwise specified, use the default
    if (device == "") device = "10EE:903F";

    // If there was no region specified on the command line, try fetching it from
    // the environment variable
    if (pciRegion == -1)
    {
        p = getenv("pcireg_region");
        if (p) pciRegion = strtoul(p, 0, 0);
    }

    // If no region is otherwise specified, use the default
    if (pciRegion == -1) pciRegion = 0;

    try
    {
        execute();
    }
    catch(const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
        exit(1);
    }
}
//=================================================================================================


//=================================================================================================
// showHelp() - Displays some help-text and exits the program
//=================================================================================================
void showHelp()
{
    printf("pcireg v1.1\n");
    printf("pcireg [-hex] [-dec] [-r <region#>] [-d <vendor>:<device>] <address> [data]\n");
    exit(1);
}
//=================================================================================================



//=================================================================================================
// strToBin() - This strips out underscores from the token then calls "strtoul" and return the
//              resulting value
//=================================================================================================
uint32_t strToBin(const char* str)
{
    char token[100], *out=token;

    // Skip over whitespace
    while (*str == 32 || *str == 9) ++str;

    // Loop through every character of the input string
    while (true)
    {
        // Fetch the next character
        int c = *str++;

        // If this character is an underscore, skip it
        if (c == '_') continue;

        // If this character is the end of the token, break
        if (c == 0  || c ==  '\n' || c == '\r' || c == 32 || c == 9) break;

        // Output the character to the token buffer.
        *out++ = c;

        // There's no way a token should exceed 90 characters
        if ((out - token) > 90) break;
    }

    // Nul-terminate the buffer
    *out = 0;

    // Hand the caller the value of the token
    return strtoul(token, 0, 0);
}
//=================================================================================================



//=================================================================================================
// parseCommandLine() - Parses the command line parameters
//
// On Exit:  pciRegion  = the PCI resource region to read/write
//           isAxiWrite = true if we are performing a write, false if we're performing a read
//           axiAddr    = the relative address (with the PCI region) to read/write
//           axiDatra   = if 'isAxiWrite' is true, the 32-bit data word to be written
//=================================================================================================
void parseCommandLine(const char** argv)
{
    int i=1, index = 0;

    while (true)
    {
        // Fetch the next token from the command line
        const char* token = argv[i++];

        // If we're out of tokens, we're done
        if (token == nullptr) break;

        // If it's the "-r" switch, the user is specifying a PCI resource region
        if (strcmp(token, "-r") == 0)
        {
            token = argv[i++];
            if (token == nullptr) showHelp();
            pciRegion = strtoul(token, 0, 0);
            continue;
        }

        // If it's the "-d" switch, the user is specifying a device code
        if (strcmp(token, "-d") == 0)
        {
            token = argv[i++];
            if (token == nullptr) showHelp();
            device = token;
            continue;
        }

        if (strcmp(token, "-dec") == 0)
        {
            output_mode |= OM_DEC;
            continue;            
        }

        if (strcmp(token, "-hex") == 0)
        {
            output_mode |= OM_HEX;
            continue;            
        }


        // Store this parameter into either "address" or "data"
        if (++index == 1)
            axiAddr = strToBin(token);
        else
        {
            axiData = strToBin(token);
            isAxiWrite = true;
        }
    }

    // If the user failed to give us an address, that's fatal
    if (axiAddr == 0xFFFFFFFF) showHelp();
}
//=================================================================================================


//=================================================================================================
// execute() - Performs the bulk of the work
//=================================================================================================
void execute()
{
    // Map the PCI memory-mapped resource regions into user-space
    PCI.open(device);

    // Fetch the list of memory mapped resource regions
    auto resource = PCI.resourceList();

    // If the user told us to use a non-existent PCI resource region, that's fatal
    if (pciRegion < 0 || pciRegion >= resource.size())
    {
        throw runtime_error("illegal PCI region");
    }

    // If the user told us to use an AXI address that's outside of our region, that's fatal    
    if (axiAddr >= resource[pciRegion].size)
    {
        throw runtime_error("illegal AXI address");
    }

    // Get a reference to this AXI register
    uint32_t& axiReg = *(uint32_t*)(resource[pciRegion].baseAddr + axiAddr);

    // Either write the data to the register, or read it and display the result
    if (isAxiWrite)
        axiReg = axiData;
    else
    {
        axiData = axiReg;
        switch (output_mode)
        {
            case OM_DEC:   printf("%u\n", axiData);
                           break;
            case OM_HEX:   printf("%08X\n", axiData);
                           break;
            case OM_BOTH:  printf("%u %08X\n", axiData, axiData);
                           break;
            default:       printf("0x%08X (%u)\n", axiData, axiData);        
        }
    }
}
//=================================================================================================
