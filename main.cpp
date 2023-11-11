#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdexcept>
#include "PciDevice.h"

using namespace std;

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
    printf("pcireg [-r <region#>] [-d <vendor>:<device>] <address> [data]\n");
    exit(1);
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

        // Store this parameter into either "address" or "data"
        if (++index == 1)
            axiAddr = strtoul(token, 0, 0);
        else
        {
            axiData = strtoul(token, 0, 0);
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
        printf("0x%08X (%u)\n", axiData, axiData);
    }
}
//=================================================================================================
