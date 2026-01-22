//=================================================================================================
// pcireg - Tool for command-line read/write of PCI device registers
//
// Author: D. Wolf
//
// Limitation:    axiAddr is restricted to 32-bits.  This can be fixed in a later version.
//
// Ver    Date       Who  What
//---------------------------------------------------------------------------------------------
// 1.3    18-Aug-25  DWW  Added support for "direct" mode (direct reading/writing an address)
//
// 1.4    21-Jan-26  DWW  Added support for the "-bdf" command line option
//=================================================================================================
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdexcept>
#include "PciDevice.h"
#include "tokenizer.h"

using namespace std;


const int OM_NONE = 0;
const int OM_DEC  = 1;
const int OM_HEX  = 2;
const int OM_BOTH = 3;    
int output_mode = OM_NONE;

bool      wide        = false;
int       pciRegion   = -1;
bool      isAxiWrite  = false;
uint32_t  axiAddr = 0xFFFFFFFF;
uint64_t  axiData;
string    device;
string    symbolFile;
int       vendorID;
int       deviceID;
string    bdf;
string    symbol;
PciDevice PCI;



void     showHelp();
void     parseCommandLine(const char** argv);
void     writeRegister(uint8_t* base_addr, uint32_t axi_addr, uint64_t data, bool wide);
uint64_t readRegister (uint8_t* base_addr, uint32_t axi_addr,                bool wide);
void     writeField   (uint8_t* base_addr, uint32_t axi_addr, uint64_t data, uint32_t fieldSpec);
uint64_t readField    (uint8_t* base_addr, uint32_t axi_addr,                uint32_t fieldSpec);
void     execute();
uint64_t getSymbolValue(std::string symbol, std::string symbolFile);

// On ARM architecture, assume we normally want direct memory access
#ifdef __aarch64__
    const char* DEFAULT_DEVICE = "direct";
#else
    const char* DEFAULT_DEVICE = "10ee:903f";
#endif

//=================================================================================================
// main() - Execution starts here.  See "showHelp()" for command line 
//=================================================================================================
int main(int argc, const char** argv)
{
    char* p;

    parseCommandLine(argv); 

    // If there was no device specified on the command line, try fetching it from
    // the environment variable
    if (device.empty()) 
    {
        p = getenv("pcireg_device");
        if (p) device = p;
    }

    // If no device is otherwise specified, use the default
    if (device.empty()) device = DEFAULT_DEVICE;

    // If there was no BDF specified on the command line, try fetching it from
    // the environment variable
    if (bdf.empty())
    {
        p = getenv("pcireg_bdf");
        if (p) bdf = p;        
    }

    // If there was no region specified on the command line, try fetching it from
    // the environment variable
    if (pciRegion == -1)
    {
        p = getenv("pcireg_region");
        if (p) pciRegion = strtoul(p, 0, 0);
    }

    // If no region is otherwise specified, use the default
    if (pciRegion == -1) pciRegion = 0;

    // If no symbol file was given, try fetching it from the environment variable
    if (symbolFile.empty())
    {
        p = getenv("pcireg_symbols");
        if (p) symbolFile = p;
    };

    // If we still don't have a symbol file, use "fpga_reg.h"
    if (symbolFile.empty()) symbolFile = "fpga_reg.h";

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
    printf("pcireg v1.4\n");
    printf("pcireg [-hex] [-dec] [-wide] [-r <region#>] [-d <vendor>:<device>] [-bdf <BDF>] [-sym <filename>] <address> [data]\n");
    exit(1);
}
//=================================================================================================



//=================================================================================================
// stripUnderscores()) - This strips out underscores from a token 
//
// This routine assumes the caller's "token" field is at least 100 bytes long
//=================================================================================================
void stripUnderscores(const char* str, char* token)
{
    // Point to the caller's output field
    char *out=token;

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
}
//=================================================================================================



//=================================================================================================
// strToBin32() - This strips out underscores from the token then calls "strtoul" and return the
//                resulting value
//=================================================================================================
uint32_t strToBin32(const char* str)
{
    char token[100];
    
    // Strip the underscores from the token
    stripUnderscores(str, token);

    // Hand the caller the value of the token
    return strtoul(token, 0, 0);
}
//=================================================================================================


//=================================================================================================
// strToBin64() - This strips out underscores from the token then calls "strtoul" and return the
//                resulting value
//=================================================================================================
uint64_t strToBin64(const char* str)
{
    char token[100];
    
    // Strip the underscores from the token
    stripUnderscores(str, token);

    // Hand the caller the value of the token
    return strtoull(token, 0, 0);
}
//=================================================================================================




//=================================================================================================
// parseCommandLine() - Parses the command line parameters
//
// On Exit:  pciRegion  = the PCI resource region to read/write
//           isAxiWrite = true if we are performing a write, false if we're performing a read
//           axiAddr    = the relative address (with the PCI region) to read/write
//           axiData    = if 'isAxiWrite' is true, the 32-bit data word to be written
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


        // If it's the "-bdf" switch, the user is specifying a PCI BDF
        if (strcmp(token, "-bdf") == 0)
        {
            token = argv[i++];
            if (token == nullptr) showHelp();
            bdf = token;
            continue;
        }


        // If the user wants the output in decimal
        if (strcmp(token, "-dec") == 0)
        {
            output_mode |= OM_DEC;
            continue;            
        }

        // If the user wants the output in hex...
        if (strcmp(token, "-hex") == 0)
        {
            output_mode |= OM_HEX;
            continue;            
        }

        // If the user wants to perform a 64-bit read/write...
        if (strcmp(token, "-wide") == 0)
        {
            wide = true;
            continue;
        }

        // If the user is giving us the name of a symbol file
        if (strcmp(token, "-sym") == 0)
        {
            token = argv[i++];
            if (token == nullptr) showHelp();
            symbolFile = token;
            continue;
        }

        // Store this parameter into either "address", "symbol" or "data"
        if (++index == 1)
        {
            if (token[0] >= '0' && token[0] <= '9')
                axiAddr = strToBin32(token);
            else
                symbol = token;
        }
        else
        {
            axiData = strToBin64(token);
            isAxiWrite = true;
        }
    }

    // If the user failed to give us an address, that's fatal
    if ((axiAddr == 0xFFFFFFFF) & symbol.empty()) showHelp();
}
//=================================================================================================


//=================================================================================================
// execute() - Performs the bulk of the work
//=================================================================================================
void execute()
{
    uint64_t symbolValue;
    uint32_t fieldSpec = 0;

    // Map the PCI memory-mapped resource regions into user-space
    if (device == "direct")
        PCI.openDirect(axiAddr, 0x1000);
    else
        PCI.open(device, bdf);

    // If we're in direct-access mode, we only care about the lower 12
    // bits of the AXI address
    if (device == "direct") axiAddr &= 0xFFF;

    // Fetch the list of memory mapped resource regions
    auto resource = PCI.resourceList();

    // If the user told us to use a non-existent PCI resource region, that's fatal
    if (pciRegion < 0 || pciRegion >= resource.size())
    {
        throw runtime_error("illegal PCI region");
    }

    // Fetch the userspace address of the PCIe resource
    uint8_t* baseAddr = (resource[pciRegion].baseAddr);

    // If the user specified the address as a symbol...
    if (!symbol.empty())
    {
        // Look up the value of the symbol
        symbolValue = getSymbolValue(symbol, symbolFile);
        
        // The address of the register is the lower 32 bits of the symbol value
        axiAddr = (uint32_t)(symbolValue & 0xFFFFFFFF);

        // The field specifier is the upper 32-bits of the symbol value
        fieldSpec = (symbolValue >> 32);

        // A field-specifier of 0x20000000 is the same as 0
        if (fieldSpec == 0x20000000) fieldSpec = 0;
    }

    // If the user told us to use an AXI address that's outside of our region, that's fatal    
    if (axiAddr >= resource[pciRegion].size)
    {
        throw runtime_error("illegal AXI address");
    }

    // If we're writing a value (i.e., not reading one) make it so
    if (isAxiWrite)
    {
        if (fieldSpec == 0)
            writeRegister(baseAddr, axiAddr, axiData, wide);
        else
            writeField(baseAddr, axiAddr, axiData, fieldSpec);
        return;
    }

    // If we get here, we're reading a register or a field within a register.
    // Field reads are never wide, they are always within a single 32-bit register
    if (fieldSpec == 0)
        axiData = readRegister(baseAddr, axiAddr, wide);
    else 
    {
        wide = false;
        axiData = readField(baseAddr, axiAddr, fieldSpec);
    }

        
    // Display the data we read
    if (wide) switch (output_mode)
    {
        case OM_DEC:   printf("%lu\n", axiData);
                       break;
        case OM_HEX:   printf("%016lX\n", axiData);
                       break;
        case OM_BOTH:  printf("%lu %016lX\n", axiData, axiData);
                       break;
        default:       printf("0x%016lX (%lu)\n", axiData, axiData);        
    }
    else switch (output_mode)
    {
        case OM_DEC:   printf("%lu\n", axiData);
                       break;
        case OM_HEX:   printf("%08lX\n", axiData);
                       break;
        case OM_BOTH:  printf("%lu %08lX\n", axiData, axiData);
                       break;
        default:       printf("0x%08lX (%lu)\n", axiData, axiData);        
    }

}
//=================================================================================================




//=================================================================================================
// writeRegister- Writes either :
//                  A single 32-bit value in a register
//                         -- or --
//                  A pair of 32-bit values into adjacent registers
//=================================================================================================
void writeRegister(uint8_t* base_addr, uint32_t axi_addr, uint64_t data, bool wide)
{
    // Get the userspace address of this register
    uint32_t* addr = (uint32_t*)(base_addr + axi_addr);

    // If we're supposed to write the upper 32-bits to a register make it so
    if (wide) *addr++ = (uint32_t)(data >> 32);

    // And write the lower 32-bits of the value into the register
    *addr = (uint32_t)(data & 0xFFFFFFFF);
}
//=================================================================================================


//=================================================================================================
// readRegister - Reads either :
//                   A single 32-bit value in a register
//                          -- or --
//                   A pair of 32-bit values into adjacent registers
//=================================================================================================
uint64_t readRegister(uint8_t* base_addr, uint32_t axi_addr, bool wide)
{
    // Get the userspace address of this register
    uint32_t* addr = (uint32_t*)(base_addr + axi_addr);

    // If we're returning a 64-bit value, read both registers
    if (wide)
    {
        uint64_t hi = addr[0];
        uint64_t lo = addr[1];
        return (hi << 32) | lo;        
    }
    
    // Otherwise, just return whatever is stored at the single 32-bit register
    return addr[0];
}
//=================================================================================================



//=================================================================================================
// writeField - Writes a specific bit-field within a register
//=================================================================================================
void writeField(uint8_t* base_addr, uint32_t axi_addr, uint64_t data, uint32_t fieldSpec)
{   
    // Get the userspace address of this register
    uint32_t* addr = (uint32_t*)(base_addr + axi_addr);

    // Find the current value of the register
    uint32_t currentValue = *addr;

    // Fetch the bit-field's width, and the position of the right-most bit
    uint32_t width = (fieldSpec >> 24) & 0xFF;
    uint32_t pos   = (fieldSpec >> 16) & 0xFF;

    // This is all 1's in the right-most 'width' bits
    uint32_t mask = (1 << width) - 1;

    // Mask off any invalid bits of the data we're going to write
    uint32_t maskedData = (uint32_t)(data & mask);

    // In the newValue, set all bits of this field to zero
    uint32_t newValue = currentValue & ~(mask << pos);

    // Stamp the data value into the bit-field
    newValue |= (maskedData << pos);

    // And store the new value into the register
    *addr = newValue;
}
//=================================================================================================





//=================================================================================================
// readField - Reads a specific bit-field within a register
//=================================================================================================
uint64_t readField(uint8_t* base_addr, uint32_t axi_addr, uint32_t fieldSpec)
{   
    // Get the userspace address of this register
    uint32_t* addr = (uint32_t*)(base_addr + axi_addr);

    // Find the current value of the register
    uint32_t currentValue = *addr;

    // Fetch the bit-field's width, and the position of the right-most bit
    uint32_t width = (fieldSpec >> 24) & 0xFF;
    uint32_t pos   = (fieldSpec >> 16) & 0xFF;

    // This is all 1's in the right-most 'width' bits
    uint32_t mask = (1 << width) - 1;

    // Hand the caller the value of this bit-field
    return (currentValue >> pos) & mask;
}
//=================================================================================================




//=============================================================================
// getSymbolValue() - Returns the 64-bit value that corresponds to the
//                    specified symbol
//=============================================================================
uint64_t getSymbolValue(string symbol, string symbolFile)
{
    char line[10000];
    CTokenizer tokenizer;
    string     err;

    // Open the input file
    FILE* ifile = fopen(symbolFile.c_str(), "r");

    // If we can't open the input file, complain
    if (ifile == nullptr)
    {
        err = "pcireg : cant open symbol file" + symbolFile;
        throw runtime_error(err);
    }

    // Loop through the symbol file...
    while (fgets(line, sizeof(line), ifile))
    {
        // Point to the first character of the line
        char* in = line;

        // Skip over spaces and tabs
        while (*in == 32 || *in == 9) ++in;

        // If it's the end of the line, ignore the line
        if (*in == 10 || *in == 13 || *in == 0) continue;

        // If it's a comment, ignore the line
        if (in[0] == '/' && in[1] == '/') continue;

        // Parse this line into tokens
        vector<string> token = tokenizer.parse(in);

        // If there aren't exactly 3 tokens on the line, ignore the line
        if (token.size() != 3) continue;

        // If the first token isn't "#define", ignore the line
        if (token[0] != "#define") continue;

        // The 2nd token on the line is the name of the register
        string& registerName = token[1];

        // If this is the symbol we're looking for...
        if (registerName == symbol)
        {
            // Done with the input file
            fclose(ifile);
        
            // The 3rd token on the line is the 64-bit address of the register
            return strtoull(token[2].c_str(), nullptr, 0);
        }
    }

    // We're done with the input file
    fclose(ifile);

    // And complain that we couldn't find the symbol
    err = "pcireg : cant find "+symbol+" in "+symbolFile;
    throw runtime_error(err);

    // This is just to keep the compiler happy
    return 0;
}
//=============================================================================


