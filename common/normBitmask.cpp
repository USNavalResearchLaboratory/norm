
#include "normBitmask.h"
#include "debug.h"
#include "sysdefs.h"


// Hamming weights for given one-byte bit masks
static unsigned char WEIGHT[256] = 
{
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};

// Bit location vectors for given one-byte bit masks
static unsigned char BITLOCS[256][8] = 
{
    {0, 0, 0, 0, 0, 0, 0, 0}, {7, 0, 0, 0, 0, 0, 0, 0}, 
    {6, 0, 0, 0, 0, 0, 0, 0}, {6, 7, 0, 0, 0, 0, 0, 0}, 
    {5, 0, 0, 0, 0, 0, 0, 0}, {5, 7, 0, 0, 0, 0, 0, 0}, 
    {5, 6, 0, 0, 0, 0, 0, 0}, {5, 6, 7, 0, 0, 0, 0, 0}, 
    {4, 0, 0, 0, 0, 0, 0, 0}, {4, 7, 0, 0, 0, 0, 0, 0}, 
    {4, 6, 0, 0, 0, 0, 0, 0}, {4, 6, 7, 0, 0, 0, 0, 0}, 
    {4, 5, 0, 0, 0, 0, 0, 0}, {4, 5, 7, 0, 0, 0, 0, 0}, 
    {4, 5, 6, 0, 0, 0, 0, 0}, {4, 5, 6, 7, 0, 0, 0, 0}, 
    {3, 0, 0, 0, 0, 0, 0, 0}, {3, 7, 0, 0, 0, 0, 0, 0}, 
    {3, 6, 0, 0, 0, 0, 0, 0}, {3, 6, 7, 0, 0, 0, 0, 0}, 
    {3, 5, 0, 0, 0, 0, 0, 0}, {3, 5, 7, 0, 0, 0, 0, 0}, 
    {3, 5, 6, 0, 0, 0, 0, 0}, {3, 5, 6, 7, 0, 0, 0, 0}, 
    {3, 4, 0, 0, 0, 0, 0, 0}, {3, 4, 7, 0, 0, 0, 0, 0}, 
    {3, 4, 6, 0, 0, 0, 0, 0}, {3, 4, 6, 7, 0, 0, 0, 0}, 
    {3, 4, 5, 0, 0, 0, 0, 0}, {3, 4, 5, 7, 0, 0, 0, 0}, 
    {3, 4, 5, 6, 0, 0, 0, 0}, {3, 4, 5, 6, 7, 0, 0, 0}, 
    {2, 0, 0, 0, 0, 0, 0, 0}, {2, 7, 0, 0, 0, 0, 0, 0}, 
    {2, 6, 0, 0, 0, 0, 0, 0}, {2, 6, 7, 0, 0, 0, 0, 0}, 
    {2, 5, 0, 0, 0, 0, 0, 0}, {2, 5, 7, 0, 0, 0, 0, 0}, 
    {2, 5, 6, 0, 0, 0, 0, 0}, {2, 5, 6, 7, 0, 0, 0, 0}, 
    {2, 4, 0, 0, 0, 0, 0, 0}, {2, 4, 7, 0, 0, 0, 0, 0}, 
    {2, 4, 6, 0, 0, 0, 0, 0}, {2, 4, 6, 7, 0, 0, 0, 0}, 
    {2, 4, 5, 0, 0, 0, 0, 0}, {2, 4, 5, 7, 0, 0, 0, 0}, 
    {2, 4, 5, 6, 0, 0, 0, 0}, {2, 4, 5, 6, 7, 0, 0, 0}, 
    {2, 3, 0, 0, 0, 0, 0, 0}, {2, 3, 7, 0, 0, 0, 0, 0}, 
    {2, 3, 6, 0, 0, 0, 0, 0}, {2, 3, 6, 7, 0, 0, 0, 0}, 
    {2, 3, 5, 0, 0, 0, 0, 0}, {2, 3, 5, 7, 0, 0, 0, 0}, 
    {2, 3, 5, 6, 0, 0, 0, 0}, {2, 3, 5, 6, 7, 0, 0, 0}, 
    {2, 3, 4, 0, 0, 0, 0, 0}, {2, 3, 4, 7, 0, 0, 0, 0}, 
    {2, 3, 4, 6, 0, 0, 0, 0}, {2, 3, 4, 6, 7, 0, 0, 0}, 
    {2, 3, 4, 5, 0, 0, 0, 0}, {2, 3, 4, 5, 7, 0, 0, 0}, 
    {2, 3, 4, 5, 6, 0, 0, 0}, {2, 3, 4, 5, 6, 7, 0, 0}, 
    {1, 0, 0, 0, 0, 0, 0, 0}, {1, 7, 0, 0, 0, 0, 0, 0}, 
    {1, 6, 0, 0, 0, 0, 0, 0}, {1, 6, 7, 0, 0, 0, 0, 0}, 
    {1, 5, 0, 0, 0, 0, 0, 0}, {1, 5, 7, 0, 0, 0, 0, 0}, 
    {1, 5, 6, 0, 0, 0, 0, 0}, {1, 5, 6, 7, 0, 0, 0, 0}, 
    {1, 4, 0, 0, 0, 0, 0, 0}, {1, 4, 7, 0, 0, 0, 0, 0}, 
    {1, 4, 6, 0, 0, 0, 0, 0}, {1, 4, 6, 7, 0, 0, 0, 0}, 
    {1, 4, 5, 0, 0, 0, 0, 0}, {1, 4, 5, 7, 0, 0, 0, 0}, 
    {1, 4, 5, 6, 0, 0, 0, 0}, {1, 4, 5, 6, 7, 0, 0, 0}, 
    {1, 3, 0, 0, 0, 0, 0, 0}, {1, 3, 7, 0, 0, 0, 0, 0}, 
    {1, 3, 6, 0, 0, 0, 0, 0}, {1, 3, 6, 7, 0, 0, 0, 0}, 
    {1, 3, 5, 0, 0, 0, 0, 0}, {1, 3, 5, 7, 0, 0, 0, 0}, 
    {1, 3, 5, 6, 0, 0, 0, 0}, {1, 3, 5, 6, 7, 0, 0, 0}, 
    {1, 3, 4, 0, 0, 0, 0, 0}, {1, 3, 4, 7, 0, 0, 0, 0}, 
    {1, 3, 4, 6, 0, 0, 0, 0}, {1, 3, 4, 6, 7, 0, 0, 0}, 
    {1, 3, 4, 5, 0, 0, 0, 0}, {1, 3, 4, 5, 7, 0, 0, 0}, 
    {1, 3, 4, 5, 6, 0, 0, 0}, {1, 3, 4, 5, 6, 7, 0, 0}, 
    {1, 2, 0, 0, 0, 0, 0, 0}, {1, 2, 7, 0, 0, 0, 0, 0}, 
    {1, 2, 6, 0, 0, 0, 0, 0}, {1, 2, 6, 7, 0, 0, 0, 0}, 
    {1, 2, 5, 0, 0, 0, 0, 0}, {1, 2, 5, 7, 0, 0, 0, 0}, 
    {1, 2, 5, 6, 0, 0, 0, 0}, {1, 2, 5, 6, 7, 0, 0, 0}, 
    {1, 2, 4, 0, 0, 0, 0, 0}, {1, 2, 4, 7, 0, 0, 0, 0}, 
    {1, 2, 4, 6, 0, 0, 0, 0}, {1, 2, 4, 6, 7, 0, 0, 0}, 
    {1, 2, 4, 5, 0, 0, 0, 0}, {1, 2, 4, 5, 7, 0, 0, 0}, 
    {1, 2, 4, 5, 6, 0, 0, 0}, {1, 2, 4, 5, 6, 7, 0, 0}, 
    {1, 2, 3, 0, 0, 0, 0, 0}, {1, 2, 3, 7, 0, 0, 0, 0}, 
    {1, 2, 3, 6, 0, 0, 0, 0}, {1, 2, 3, 6, 7, 0, 0, 0}, 
    {1, 2, 3, 5, 0, 0, 0, 0}, {1, 2, 3, 5, 7, 0, 0, 0}, 
    {1, 2, 3, 5, 6, 0, 0, 0}, {1, 2, 3, 5, 6, 7, 0, 0}, 
    {1, 2, 3, 4, 0, 0, 0, 0}, {1, 2, 3, 4, 7, 0, 0, 0}, 
    {1, 2, 3, 4, 6, 0, 0, 0}, {1, 2, 3, 4, 6, 7, 0, 0}, 
    {1, 2, 3, 4, 5, 0, 0, 0}, {1, 2, 3, 4, 5, 7, 0, 0}, 
    {1, 2, 3, 4, 5, 6, 0, 0}, {1, 2, 3, 4, 5, 6, 7, 0}, 
    {0, 0, 0, 0, 0, 0, 0, 0}, {0, 7, 0, 0, 0, 0, 0, 0}, 
    {0, 6, 0, 0, 0, 0, 0, 0}, {0, 6, 7, 0, 0, 0, 0, 0}, 
    {0, 5, 0, 0, 0, 0, 0, 0}, {0, 5, 7, 0, 0, 0, 0, 0}, 
    {0, 5, 6, 0, 0, 0, 0, 0}, {0, 5, 6, 7, 0, 0, 0, 0}, 
    {0, 4, 0, 0, 0, 0, 0, 0}, {0, 4, 7, 0, 0, 0, 0, 0}, 
    {0, 4, 6, 0, 0, 0, 0, 0}, {0, 4, 6, 7, 0, 0, 0, 0}, 
    {0, 4, 5, 0, 0, 0, 0, 0}, {0, 4, 5, 7, 0, 0, 0, 0}, 
    {0, 4, 5, 6, 0, 0, 0, 0}, {0, 4, 5, 6, 7, 0, 0, 0}, 
    {0, 3, 0, 0, 0, 0, 0, 0}, {0, 3, 7, 0, 0, 0, 0, 0}, 
    {0, 3, 6, 0, 0, 0, 0, 0}, {0, 3, 6, 7, 0, 0, 0, 0}, 
    {0, 3, 5, 0, 0, 0, 0, 0}, {0, 3, 5, 7, 0, 0, 0, 0}, 
    {0, 3, 5, 6, 0, 0, 0, 0}, {0, 3, 5, 6, 7, 0, 0, 0}, 
    {0, 3, 4, 0, 0, 0, 0, 0}, {0, 3, 4, 7, 0, 0, 0, 0}, 
    {0, 3, 4, 6, 0, 0, 0, 0}, {0, 3, 4, 6, 7, 0, 0, 0}, 
    {0, 3, 4, 5, 0, 0, 0, 0}, {0, 3, 4, 5, 7, 0, 0, 0}, 
    {0, 3, 4, 5, 6, 0, 0, 0}, {0, 3, 4, 5, 6, 7, 0, 0}, 
    {0, 2, 0, 0, 0, 0, 0, 0}, {0, 2, 7, 0, 0, 0, 0, 0}, 
    {0, 2, 6, 0, 0, 0, 0, 0}, {0, 2, 6, 7, 0, 0, 0, 0}, 
    {0, 2, 5, 0, 0, 0, 0, 0}, {0, 2, 5, 7, 0, 0, 0, 0}, 
    {0, 2, 5, 6, 0, 0, 0, 0}, {0, 2, 5, 6, 7, 0, 0, 0}, 
    {0, 2, 4, 0, 0, 0, 0, 0}, {0, 2, 4, 7, 0, 0, 0, 0}, 
    {0, 2, 4, 6, 0, 0, 0, 0}, {0, 2, 4, 6, 7, 0, 0, 0}, 
    {0, 2, 4, 5, 0, 0, 0, 0}, {0, 2, 4, 5, 7, 0, 0, 0}, 
    {0, 2, 4, 5, 6, 0, 0, 0}, {0, 2, 4, 5, 6, 7, 0, 0}, 
    {0, 2, 3, 0, 0, 0, 0, 0}, {0, 2, 3, 7, 0, 0, 0, 0}, 
    {0, 2, 3, 6, 0, 0, 0, 0}, {0, 2, 3, 6, 7, 0, 0, 0}, 
    {0, 2, 3, 5, 0, 0, 0, 0}, {0, 2, 3, 5, 7, 0, 0, 0}, 
    {0, 2, 3, 5, 6, 0, 0, 0}, {0, 2, 3, 5, 6, 7, 0, 0}, 
    {0, 2, 3, 4, 0, 0, 0, 0}, {0, 2, 3, 4, 7, 0, 0, 0}, 
    {0, 2, 3, 4, 6, 0, 0, 0}, {0, 2, 3, 4, 6, 7, 0, 0}, 
    {0, 2, 3, 4, 5, 0, 0, 0}, {0, 2, 3, 4, 5, 7, 0, 0}, 
    {0, 2, 3, 4, 5, 6, 0, 0}, {0, 2, 3, 4, 5, 6, 7, 0}, 
    {0, 1, 0, 0, 0, 0, 0, 0}, {0, 1, 7, 0, 0, 0, 0, 0}, 
    {0, 1, 6, 0, 0, 0, 0, 0}, {0, 1, 6, 7, 0, 0, 0, 0}, 
    {0, 1, 5, 0, 0, 0, 0, 0}, {0, 1, 5, 7, 0, 0, 0, 0}, 
    {0, 1, 5, 6, 0, 0, 0, 0}, {0, 1, 5, 6, 7, 0, 0, 0}, 
    {0, 1, 4, 0, 0, 0, 0, 0}, {0, 1, 4, 7, 0, 0, 0, 0}, 
    {0, 1, 4, 6, 0, 0, 0, 0}, {0, 1, 4, 6, 7, 0, 0, 0}, 
    {0, 1, 4, 5, 0, 0, 0, 0}, {0, 1, 4, 5, 7, 0, 0, 0}, 
    {0, 1, 4, 5, 6, 0, 0, 0}, {0, 1, 4, 5, 6, 7, 0, 0}, 
    {0, 1, 3, 0, 0, 0, 0, 0}, {0, 1, 3, 7, 0, 0, 0, 0}, 
    {0, 1, 3, 6, 0, 0, 0, 0}, {0, 1, 3, 6, 7, 0, 0, 0}, 
    {0, 1, 3, 5, 0, 0, 0, 0}, {0, 1, 3, 5, 7, 0, 0, 0}, 
    {0, 1, 3, 5, 6, 0, 0, 0}, {0, 1, 3, 5, 6, 7, 0, 0}, 
    {0, 1, 3, 4, 0, 0, 0, 0}, {0, 1, 3, 4, 7, 0, 0, 0}, 
    {0, 1, 3, 4, 6, 0, 0, 0}, {0, 1, 3, 4, 6, 7, 0, 0}, 
    {0, 1, 3, 4, 5, 0, 0, 0}, {0, 1, 3, 4, 5, 7, 0, 0}, 
    {0, 1, 3, 4, 5, 6, 0, 0}, {0, 1, 3, 4, 5, 6, 7, 0}, 
    {0, 1, 2, 0, 0, 0, 0, 0}, {0, 1, 2, 7, 0, 0, 0, 0}, 
    {0, 1, 2, 6, 0, 0, 0, 0}, {0, 1, 2, 6, 7, 0, 0, 0}, 
    {0, 1, 2, 5, 0, 0, 0, 0}, {0, 1, 2, 5, 7, 0, 0, 0}, 
    {0, 1, 2, 5, 6, 0, 0, 0}, {0, 1, 2, 5, 6, 7, 0, 0}, 
    {0, 1, 2, 4, 0, 0, 0, 0}, {0, 1, 2, 4, 7, 0, 0, 0}, 
    {0, 1, 2, 4, 6, 0, 0, 0}, {0, 1, 2, 4, 6, 7, 0, 0}, 
    {0, 1, 2, 4, 5, 0, 0, 0}, {0, 1, 2, 4, 5, 7, 0, 0}, 
    {0, 1, 2, 4, 5, 6, 0, 0}, {0, 1, 2, 4, 5, 6, 7, 0}, 
    {0, 1, 2, 3, 0, 0, 0, 0}, {0, 1, 2, 3, 7, 0, 0, 0}, 
    {0, 1, 2, 3, 6, 0, 0, 0}, {0, 1, 2, 3, 6, 7, 0, 0}, 
    {0, 1, 2, 3, 5, 0, 0, 0}, {0, 1, 2, 3, 5, 7, 0, 0}, 
    {0, 1, 2, 3, 5, 6, 0, 0}, {0, 1, 2, 3, 5, 6, 7, 0}, 
    {0, 1, 2, 3, 4, 0, 0, 0}, {0, 1, 2, 3, 4, 7, 0, 0}, 
    {0, 1, 2, 3, 4, 6, 0, 0}, {0, 1, 2, 3, 4, 6, 7, 0}, 
    {0, 1, 2, 3, 4, 5, 0, 0}, {0, 1, 2, 3, 4, 5, 7, 0}, 
    {0, 1, 2, 3, 4, 5, 6, 0}, {0, 1, 2, 3, 4, 5, 6, 7}
};

NormBitmask::NormBitmask()
    : mask(NULL), mask_len(0), 
      num_bits(0), first_set(0)
{
}

NormBitmask::~NormBitmask()
{
    Destroy();
}

bool NormBitmask::Init(unsigned long numBits)
{
    if (mask) Destroy();
    // Allocate memory for mask
    unsigned int len = (numBits + 7) >> 3;
    if ((mask = new unsigned char[len]))
    {
        num_bits = numBits;
        mask_len = len;
        Clear();
        return true;
    }
    else
    {
        return false;
    }
   
}  // end NormBitmask::Init()

void NormBitmask::Destroy()
{
    if (mask) 
    {
        delete[] mask;
        mask = (unsigned char*)NULL;
        num_bits = first_set = 0;
    }
}  // end NormBitmask::Destroy()


bool NormBitmask::GetNextSet(unsigned long& index) const
{   
    if (index >= num_bits) return false;
    if (index < first_set) return GetFirstSet(index);
    unsigned long maskIndex = index >> 3;
    if (mask[maskIndex])
    {
        int w = WEIGHT[mask[maskIndex]];
        int remainder = index & 0x07;
        for (int i = 0; i < w; i++)
        {
            int loc = BITLOCS[mask[maskIndex]][i];
            if (loc >= remainder) 
            {
                index = (maskIndex << 3) + loc;
                return true;
            }
        }
    }
    while(++maskIndex < mask_len)
    {
        if (mask[maskIndex])
        {
            index = (maskIndex << 3) +  BITLOCS[mask[maskIndex]][0];
            return true;
        }
    }
    return false;
}  // end NormBitmask::NextSet()

bool NormBitmask::GetPrevSet(unsigned long& index) const
{
    if (index >= num_bits) index = num_bits - 1;
    if (index < first_set) return false;
    unsigned long maskIndex = index >> 3;
    if (mask[maskIndex])
    {
        int w = WEIGHT[mask[maskIndex]] - 1;
        int remainder = index & 0x07;
        for (int i = w; i >= 0; i--)
        {
            int loc = BITLOCS[mask[maskIndex]][i];
            if (loc <= remainder) 
            {
                index = ((maskIndex << 3) + loc);
                return true;
            }
        }
    }
    maskIndex--;
    unsigned long startIndex = first_set >> 3;
    for (; maskIndex >= startIndex; maskIndex--)
    {
        if (mask[maskIndex])
        {
            int w = WEIGHT[mask[maskIndex]] - 1;
            index = ((maskIndex << 3) + BITLOCS[mask[maskIndex]][w]);
            return true;
        }
    }
    return false;  // (nothing prior was set)
}  // end NormBitmask::PrevSet()

bool NormBitmask::GetNextUnset(unsigned long& index) const
{
    if (index >= num_bits) return false;
    unsigned long next = index;
    unsigned long maskIndex = next >> 3;
    unsigned char bit = 0x80 >> (next & 0x07);
    while (next < num_bits)
    {
        unsigned char val = mask[maskIndex];
        if (val)
        {
            while (bit && (next < num_bits))
            {
                if (0 == (val & bit)) 
                {
                    index = next;
                    return true;
                }
                next++;   
                bit >>= 0x01;
            }
            bit = 0x80;
            maskIndex++;
        }
        else
        {
            index = next;
            return true;
        }
    }
    return false;
}  // end NormBitmask::NextUnset()


bool NormBitmask::SetBits(unsigned long index, unsigned long count)
{
    if (0 == count) return true;
    if ((index+count) > num_bits) return false;
    unsigned long maskIndex = index >> 3;
    // To set appropriate bits in first byte
    unsigned int bitIndex = index & 0x07;
    unsigned int bitRemainder = 8 - bitIndex;
    if (count <= bitRemainder)
    {
        mask[maskIndex] |= (0x00ff >> bitIndex) &
                           (0x00ff << (bitRemainder - count)); 
    }
    else
    {
        mask[maskIndex] |= 0x00ff >> bitIndex;
        count -= bitRemainder;
        unsigned long nbytes = count >> 3;  
        memset(&mask[++maskIndex], 0xff, nbytes);
        count &= 0x07;  
        if (count)
            mask[maskIndex+nbytes] |= 0xff << (8-count);
    }
    if (index < first_set) first_set = index;
    return true;
}  // end NormBitmask::SetBits()


bool NormBitmask::UnsetBits(unsigned long index, unsigned long count)
{
    if ((index >= num_bits)|| (0 == count)) return true;
    unsigned long end = index + count;
    if (end > num_bits) 
    {
        end = num_bits;
        count = end - index;
    }
    unsigned long maskIndex = index >> 3;
    // To unset appropriate bits in first byte
    unsigned int bitIndex = index & 0x07;
    unsigned int bitRemainder = 8 - bitIndex;
    if (count <= bitRemainder)
    {
        mask[maskIndex] &= (0x00ff << bitRemainder) |
                           (0x00ff >> (bitIndex + count)); 
    }
    else
    {
        mask[maskIndex] &= 0x00ff << bitRemainder;
        count -= bitRemainder;
        unsigned long nbytes = count >> 3;  
        memset(&mask[++maskIndex], 0, nbytes);
        count &= 0x07;  
        if (count) mask[maskIndex+nbytes] &= 0xff >> count;
    }
    if ((first_set >= index) && (end > first_set))
    { 
        first_set = end;
        if (!GetNextSet(first_set)) first_set = num_bits;
    }
    return true;
}  // end NormBitmask::UnsetBits()

// this = b
bool NormBitmask::Copy(const NormBitmask &b)
{
    if (b.num_bits > num_bits) return false;
    memcpy(mask, b.mask, b.mask_len);
    (mask_len > b.mask_len) ? 
        memset(mask+b.mask_len, 0, mask_len - b.mask_len) : 0;
    first_set = (b.first_set < b.num_bits) ? b.first_set : num_bits;
    return true;
}  // end NormBitmask::Copy()

// this = this | b
bool NormBitmask::Add(const NormBitmask& b)
{
    if (b.num_bits > num_bits) return false;   
    for(unsigned int i = 0; i < b.mask_len; i++)
        mask[i] |= b.mask[i];
    if (b.first_set < first_set) first_set = b.first_set;
    return true;
}  // end NormBitmask::Add()

// this = this & ~b
bool NormBitmask::Subtract(const NormBitmask& b)
{
    unsigned long len = (mask_len < b.mask_len) ? mask_len : b.mask_len;
    for(unsigned long i = 0; i < len; i++)
        mask[i] &= ~b.mask[i];
    if (first_set >= b.first_set) 
    {
        if (!GetNextSet(first_set)) first_set = num_bits;
    }
    return true;
}  // end NormBitmask::Subtract()


// this = ~this & b
bool NormBitmask::XCopy(const NormBitmask& b)
{
    if (b.num_bits > num_bits) return false;
    unsigned int len = b.mask_len;
    unsigned int begin = b.first_set >> 3;
    if (begin) memset(mask, 0, begin);
    for (unsigned int i = begin; i < len; i++)
        mask[i] = b.mask[i] & ~mask[i];
    if (len < mask_len) memset(&mask[len], 0, mask_len - len);
    if (b.first_set < first_set)
    {
        first_set = b.first_set;
    }
    else
    {
        first_set = b.first_set;
        GetNextSet(first_set);
    }
    return true;
}  // end NormBitmask::XCopy()

// this = this & b
bool NormBitmask::Multiply(const NormBitmask& b)
{
    unsigned long len = (mask_len < b.mask_len) ? mask_len : b.mask_len;   
    for(unsigned int i = 0; i < len; i++)
        mask[i] |= b.mask[i];
    if (len < mask_len) memset(&mask[len], 0, mask_len - len);
    if (b.first_set > first_set)
    {
        first_set = b.first_set;
        if (!GetNextSet(first_set)) first_set = num_bits;
    }
    else if (first_set > b.first_set)
    {
        if (!GetNextSet(first_set)) first_set = num_bits;
    }
    return true;
}  // end NormBitmask::Multiply()

// this = this ^ b
bool NormBitmask::Xor(const NormBitmask& b)
{
    if (b.num_bits > num_bits) return false;
    for(unsigned int i = 0; i < b.mask_len; i++)
        mask[i] ^= b.mask[i];
    if (b.first_set == first_set)
        GetNextSet(first_set);
    return true;
}  // end NormBitmask::Xor()

void NormBitmask::Display(FILE* stream)
{
    unsigned long index = 0;
    for (unsigned long i = 0; i < num_bits; i++)
    {
        if (Test(index++)) fprintf(stream, "1"); else fprintf(stream, "0");
        if (0x07 == (i & 0x07)) fprintf(stream, " ");
        if (0x3f == (i & 0x3f)) fprintf(stream, "\n");
    }
}  // end NormBitmask::Display()






NormSlidingMask::NormSlidingMask()
 : mask((unsigned char*)NULL), mask_len(0), num_bits(0), 
   start(0), end(0), offset(0)
{
}


NormSlidingMask::~NormSlidingMask()
{
    Destroy();
}

bool NormSlidingMask::Init(long numBits, UINT32 rangeMask)
{
    
    if (mask) Destroy();
    if (numBits <= 0) return false;
    unsigned long len = (numBits + 7) >> 3;
    if ((mask = new unsigned char[len]))
    {
        range_mask = rangeMask;
        range_sign = (rangeMask ^ (rangeMask >> 1));
        mask_len = len;
        num_bits = numBits;
        Clear();
        return true;
    }
    else
    {
        return false;
    }
}  // end NormSlidingMask::Init()

void NormSlidingMask::Destroy()
{
    if (mask)
    {
        delete[] mask;
        mask = NULL;
        mask_len = num_bits = start = end = offset = 0;
    }   
}  // end NormSlidingMask::Destroy()

bool NormSlidingMask::CanSet(unsigned long index) const
{
    if (IsSet())
    {
        // Determine position with respect to current start
        // and end, given the "offset" of the current start  
        long pos = Delta(index, offset);
        if (pos < 0)
        {
            // Precedes start.
            pos += start;
            if (pos < 0) pos += num_bits;
            if (pos < 0)
            {
                // out of range
                return false;
            }
            else
            {
                // Is pos between end & start?
                if (end < start)
                {
                   if ((pos <= end) || (pos > start)) return false; 
                }
                else
                {
                   if ((pos <= end) && (pos > start)) return false;
                }
                return true;
            } 
        }
        else if (pos < num_bits)
        {
                return true;
        }
        else
        {
                return false;  // out of range
        }
    }
    else
    {
        return true;  // empty mask can be set in any fashion
    }
}  // end NormSlidingMask::CanSet()

bool NormSlidingMask::Set(unsigned long index)
{
    ASSERT(CanSet(index));
    if (IsSet())
    {        
        // Determine position with respect to current start
        // and end, given the "offset" of the current start               
        long pos = Delta(index , offset);  
        if (pos < 0)
        {
            // Precedes start.
            pos += start;
            if (pos < 0) pos += num_bits;
            if (pos < 0)
            {
                // out of range
                return false;
            }
            else
            {
                // Is pos between end & start?
                if (end < start)
                {
                   if ((pos <= end) || (pos > start)) return false; 
                }
                else
                {
                    if ((pos <= end) && (pos > start)) return false;
                }
                // Ok for new "start" && "offset"
                start = pos;
                offset = index;
            } 
        }
        else if (pos < num_bits)
        {
            pos += start;
            if (pos >= num_bits) pos -= num_bits;
            if (end < start)
            {
                if ((pos < start) && (pos > end)) end = pos;  
            }
            else
            {
                if ((pos > end) || (pos < start)) end = pos;
            }
        }
        else
        {
            return false;  // out of range
        }
        ASSERT((pos >> 3) >= 0);
        ASSERT((pos >> 3) < (long)mask_len);
        mask[(pos >> 3)] |= (0x80 >> (pos & 0x07));
    }
    else
    {
        start = end = 0;
        offset = index;   
        mask[0] = 0x80;
    }
    return true;
}  // end NormSlidingMask::Set()

bool NormSlidingMask::Unset(unsigned long index)
{
    //ASSERT(CanSet(index));
    if (IsSet())
    {
        long pos = Delta(index, offset);
        if (pos < 0)
        {
            return true;  // out-of-range
        }
        else if (pos < num_bits)
        {
            // Is it in current range of set bits?
            pos += start;
            if (pos >= num_bits) pos -= num_bits;
            if (end < start)
            {
                if ((pos > end) && (pos < start)) return true;   
            }
            else
            {
                if ((pos < start) || (pos > end)) return true;   
            }
            // Yes, it was in range.
            // Unset the corresponding bit
            ASSERT((pos >> 3) >= 0);
            ASSERT((pos >> 3) < (long)mask_len);
            mask[(pos >> 3)] &= ~(0x80 >> (pos & 0x07));
            if (start == end) 
            {
                ASSERT(pos == start);
                start = end = num_bits;
                return true;
            }
            if (start == pos) 
            {
                unsigned long next = index;
                if (!GetNextSet(next)) ASSERT(0);
                long delta = Delta(next, offset);
                ASSERT(delta >= 0);
                start += delta;
                if (start >= num_bits) start -= num_bits;
                offset = next;
            }
            if (pos == end) 
            {
                unsigned long prev = index;
                if (!GetPrevSet(prev)) ASSERT(0);
                long delta = Delta(prev, offset);
                ASSERT(delta >= 0);
                end = start + delta;
                if (end >= num_bits) end -= num_bits;
            }
        }
        else
        {
            return true; // out-of-range   
        }
    }
    return true;
}  // end NormSlidingMask::Unset()

bool NormSlidingMask::SetBits(unsigned long index, long count)
{
    ASSERT(CanSet(index));
    ASSERT(CanSet(index+count-1));
    if (count < 0) return false;
    if (0 == count) return true;
    long firstPos, lastPos;
    if (IsSet())
    {
        long last = index + count - 1;
        if (!CanSet(index)) return false;
        if (!CanSet(last)) return false; 
        // Calculate first set bit position  
        firstPos = Delta(index, offset);
        if (firstPos < 0)
        {
            // precedes start
            firstPos += start;
            if (firstPos < 0) firstPos += num_bits;
            start = firstPos;
            offset = index;
        }
        else
        {
            firstPos += start;
            if (firstPos >= num_bits) firstPos -= num_bits;
        }
        // Calculate last set bit position
        lastPos = Delta(last , offset);
        if (lastPos > 0)
        {
            // Is post start, post end?  
            lastPos += start; 
            if (lastPos >= num_bits) lastPos -= num_bits;
            if (end < start)
            {
                if ((lastPos < start) && (lastPos > end)) end = lastPos;  
            }
            else
            {
                if ((lastPos > end) || (lastPos < start)) end = lastPos;
            }
        }
        else
        {
            lastPos += start;
            if (lastPos < 0) lastPos += num_bits;
        }
        if (lastPos < firstPos)
        {
            // Set bits from firstPos to num_bits   
            count = num_bits - firstPos;
            long maskIndex = firstPos >> 3;
            int bitIndex = firstPos & 0x07;
            int bitRemainder = 8 - bitIndex;
            ASSERT(maskIndex >= 0);
            if (count <= bitRemainder)
            {
                ASSERT(maskIndex < (long)mask_len);
                mask[maskIndex] |= (0x00ff >> bitIndex) &
                                   (0x00ff << (bitRemainder - count)); 
            }
            else
            {
                ASSERT(maskIndex < (long)mask_len);
                mask[maskIndex] |= 0x00ff >> bitIndex;
                count -= bitRemainder;
                long nbytes = count >> 3;  
                ASSERT((maskIndex+1+nbytes) <= (long)mask_len);
                memset(&mask[++maskIndex], 0xff, nbytes);
                count &= 0x07;  
                if (count) 
                {
                    ASSERT((maskIndex+nbytes) >= 0);
                    ASSERT((maskIndex+nbytes) < (long)mask_len);
                    mask[maskIndex+nbytes] |= 0xff << (8-count);
                }
            }
            firstPos = 0;
        }
    }
    else
    {
        if (count > num_bits) return false;
        start = firstPos = 0;
        end = lastPos = (long)(count - 1);
        offset = index;
    }
    // Set bits from firstPos to lastPos   
    count = lastPos - firstPos + 1;
    long maskIndex = firstPos >> 3;
    int bitIndex = firstPos & 0x07;
    int bitRemainder = 8 - bitIndex;
    ASSERT(maskIndex >= 0);
    if (count <= bitRemainder)
    {
        ASSERT(maskIndex < (long)mask_len);
        mask[maskIndex] |= (0x00ff >> bitIndex) &
                            (0x00ff << (bitRemainder - count)); 
    }
    else
    {
        ASSERT(maskIndex < (long)mask_len);
        mask[maskIndex] |= 0x00ff >> bitIndex;
        count -= bitRemainder;
        long nbytes = count >> 3;
        ASSERT((maskIndex+1+nbytes) <= (long)mask_len);  
        memset(&mask[++maskIndex], 0xff, nbytes);
        count &= 0x07;  
        if (count) 
        {
            ASSERT((maskIndex+nbytes) >= 0);
            ASSERT((maskIndex+nbytes) < (long)mask_len);
            mask[maskIndex+nbytes] |= 0xff << (8-count);
        }
    }
    return true;
}  // end NormSlidingMask::SetBits()

bool NormSlidingMask::UnsetBits(unsigned long index, long count)
{
    //ASSERT(CanSet(index));
    //ASSERT(CanSet(index+count-1));
    if (IsSet())
    {
        // Trim to fit as needed.
        long firstPos;
        if (count <= 0) return true;
        if (count > num_bits) count = num_bits;
        long delta = Delta(index , offset);
        if (delta >= num_bits)
        {
            return true;
        }
        else if (delta <= 0)
        {
            firstPos = start;  
            count += delta; 
            if (count <= 0) return true;
        }
        else
        {
            firstPos = start + delta;
            if (firstPos >= num_bits) firstPos -= num_bits;
        }
        UINT32 lastSet;
        if (!GetLastSet(lastSet)) ASSERT(0);
        delta = Delta((index+count-1), lastSet);
        long lastPos;
        if (delta < 0)
        { 
            lastPos = firstPos + count - 1;
            if (lastPos >= num_bits) lastPos -= num_bits;
        }
        else
        {
            lastPos = end;
        }
        long startPos;
        if (lastPos < firstPos)
        {
            // Set bits from firstPos to num_bits   
            count = num_bits - firstPos;
            long maskIndex = firstPos >> 3;
            int bitIndex = firstPos & 0x07;
            int bitRemainder = 8 - bitIndex;
            if (count <= bitRemainder)
            {
                ASSERT(maskIndex < (long)mask_len);
                mask[maskIndex] &= (0x00ff << bitRemainder) |
                                   (0x00ff >> (bitIndex + count));
            }
            else
            {
                ASSERT(maskIndex < (long)mask_len);
                mask[maskIndex] &= 0x00ff << bitRemainder;
                count -= bitRemainder;
                unsigned long nbytes = count >> 3;  
                ASSERT((maskIndex+1+nbytes) <= mask_len);
                memset(&mask[++maskIndex], 0, nbytes);
                count &= 0x07;  
                if (count) 
                {
                    ASSERT((maskIndex+nbytes) < mask_len);
                    mask[maskIndex+nbytes] &= 0xff >> count;
                }
            }
            startPos = 0;
        }
        else
        {
            startPos = firstPos;
        }
        // Unset bits from firstPos to lastPos   
        count = lastPos - startPos + 1;
        long maskIndex = startPos >> 3;
        int bitIndex = startPos & 0x07;
        int bitRemainder = 8 - bitIndex;
        if (count <= bitRemainder)
        {
            ASSERT(maskIndex < (long)mask_len);
            mask[maskIndex] &= (0x00ff << bitRemainder) |
                               (0x00ff >> (bitIndex + count)); 
        }
        else
        {
            ASSERT(maskIndex < (long)mask_len);
            mask[maskIndex] &= 0x00ff << bitRemainder;
            count -= bitRemainder;
            unsigned long nbytes = count >> 3;
            ASSERT((maskIndex+1+nbytes) <= mask_len);
            memset(&mask[++maskIndex], 0, nbytes);
            count &= 0x07;  
            if (count) 
            {   
                ASSERT((maskIndex+nbytes) < mask_len);
                mask[maskIndex+nbytes] &= 0xff >> count;
            }
        }
        // Calling these will properly update the offset/start/end state
        if (start == firstPos) 
        {
            if (end == lastPos)
            {
                start = end = num_bits;
            }
            else
            {
                Unset(offset);
            }
        }
        else if (end == lastPos) 
        {
            UINT32 lastSet;
            GetLastSet(lastSet);
            Unset(lastSet);
        }
    }
    return true;
}  // end NormSlidingMask::UnsetBits()

bool NormSlidingMask::Test(unsigned long index) const
{
    if (IsSet())
    {
        long pos = Delta(index , offset);
        if (pos >= 0)
        {
            // Is it in range?
            if (pos >= num_bits) return false;
            pos += start;
            if (pos >= num_bits) pos -= num_bits;
            if (end < start)
            {
                if ((pos > end) && (pos < start)) return false;   
            }
            else
            {
                if ((pos < start) || (pos > end)) return false;   
            }
            // Test the corresponding bit
            if (0 != (mask[(pos >> 3)] & (0x80 >> (pos & 0x07))))
                return true;
        }
    }
    return false;
}  // end NormSlidingMask::Test()


bool NormSlidingMask::GetNextSet(unsigned long& index) const
{
    if (IsSet())
    {
        unsigned long next = index;
        long pos = Delta(next, offset);
        if (pos >= 0)
        {
            // Is it in range?
            if (pos >= num_bits) return false;
            pos += start;
            if (pos >= num_bits) pos -= num_bits;
            
            if (end < start)
            {
                if ((pos > end) && (pos < start)) return false;
            }
            else
            {
                if ((pos < start) || (pos > end)) return false;
            }
            // Seek next set bit
            unsigned long maskIndex = pos >> 3;
            if (mask[maskIndex])
            {
                int w = WEIGHT[mask[maskIndex]];
                int remainder = pos & 0x07;
                for (int i = 0; i < w; i++)
                {
                    int loc = BITLOCS[mask[maskIndex]][i];
                    if (loc >= remainder) 
                    {
                        pos = (maskIndex << 3) + loc;
                        pos -= start;
                        if (pos < 0) pos += num_bits;
                        index = offset + pos;
                        return true;
                    }
                }
            }
            maskIndex++;
            if (end < pos)
            {
                for (; maskIndex < mask_len; maskIndex++)
                {
                    if (mask[maskIndex])
                    {
                        pos = (maskIndex << 3) + BITLOCS[mask[maskIndex]][0];
                        pos -= start;
                        if (pos < 0) pos += num_bits;
                        index =  offset + pos;
                        return true;
                    }
                }
                maskIndex = 0;
            }
            unsigned long endIndex = end >> 3;
            for (; maskIndex <= endIndex; maskIndex++)
            {
                if (mask[maskIndex])
                {
                    pos = (maskIndex << 3) + BITLOCS[mask[maskIndex]][0];
                    pos -= start;
                    if (pos < 0) pos += num_bits;
                    index = offset + pos;
                    return true;
                }
            }
        }
        else
        {
            return false;
        }
    }
    return false;  // indicates nothing was set
}  // end NormSlidingMask::GetNextSet()

bool NormSlidingMask::GetPrevSet(unsigned long& index) const
{
    if (IsSet())
    {
        unsigned long prev = index;
        long pos = Delta(prev, offset);
        if (pos >= 0)
        {
            // Is it in range?
            if (pos >= num_bits) 
            {
                return false;
            }
            pos += start;
            if (pos >= num_bits) pos -= num_bits;
            if (end < start)
            {
                if ((pos > end) && (pos < start)) return false;
            }
            else
            {
                if ((pos < start) || (pos > end)) return false;
            }
            // Seek prev set bits, starting with index   
            long maskIndex = pos >> 3;
            if (mask[maskIndex])
            {
                int w = WEIGHT[mask[maskIndex]] - 1;
                int remainder = pos & 0x07;
                for (int i = w; i >= 0; i--)
                {
                    int loc = BITLOCS[mask[maskIndex]][i];
                    if (loc <= remainder) 
                    {
                        pos = (maskIndex << 3) + loc;
                        pos -= start;
                        if (pos < 0) pos += num_bits;
                        index = offset + pos;
                        return true;
                    }
                }
            }
            maskIndex--;
            if (pos < start) 
            {
                for(; maskIndex >= 0; maskIndex--)
                {                   
                    if (mask[maskIndex])
                    {
                        
                        int w = WEIGHT[mask[maskIndex]] - 1;
                        pos =  (maskIndex << 3) + BITLOCS[mask[maskIndex]][w];
                        pos -= start;
                        if (pos < 0) pos += num_bits;
                        index = offset + pos;
                        return true;  
                    }
                } 
                maskIndex = mask_len - 1;  
            }
            long startIndex = start >> 3;
            for (; maskIndex >= startIndex; maskIndex--)
            {
                if (mask[maskIndex])
                {
                    int w = WEIGHT[mask[maskIndex]] - 1;
                    pos =  (maskIndex << 3) + BITLOCS[mask[maskIndex]][w];
                    pos -= start;
                    if (pos < 0) pos += num_bits;
                    index = offset + pos;
                    return true;  
                }
            }
        }
    }
    return false;  // indicates nothing prior was set
}  // end NormSlidingMask::GetPrevSet()

/*unsigned long NormSlidingMask::RawNextSet(const char* mask, long index, long end)
{
    // Seek next set bit
    unsigned long maskIndex = index >> 3;
    if (mask[maskIndex])
    {
        int w = WEIGHT[mask[maskIndex]];
        int remainder = index & 0x07;
        for (int i = 0; i < w; i++)
        {
            int loc = BITLOCS[mask[maskIndex]][i];
            if (loc >= remainder) 
                return ((maskIndex << 3) + loc);
        }
    }
    maskIndex++;
    unsigned long endIndex = end >> 3;
    for (; maskIndex <= endIndex; maskIndex++)
    {
        if (mask[maskIndex])
            return((maskIndex << 3) + BITLOCS[mask[maskIndex]][0]);
    }
    return (index-1);  // (nothing was set)       
}  // end NormSlidingMask::RawNextSet()

unsigned long NormSlidingMask::RawPrevSet(const char* mask, long index, long start)
{
    // Seek prev set bits, starting with index   
    unsigned long maskIndex = index >> 3;
    if (mask[maskIndex])
    {
        int w = WEIGHT[mask[maskIndex]] - 1;
        int remainder = index & 0x07;
        for (int i = w; i >= 0; i--)
        {
            int loc = BITLOCS[mask[maskIndex]][i];
            if (loc <= remainder) 
            {
                return((maskIndex << 3) + loc);
            }
        }
    }
    maskIndex--;
    unsigned long startIndex = start >> 3;
    for (; maskIndex >= startIndex; maskIndex--)
    {
        if (mask[maskIndex])
        {
            int w = WEIGHT[mask[maskIndex]] - 1;
            return((maskIndex << 3) + BITLOCS[mask[maskIndex]][w]);
        }
    }
    return (index+1);  // (nothing was set)
}  // end NormSlidingMask::RawPrevSet()*/

bool NormSlidingMask::Copy(const NormSlidingMask& b)
{
    if (b.IsSet())
    {
        long range = b.end - b.start;
        if (range < 0) range += b.num_bits;
        if (range <= num_bits)
        {
            start = b.start & 0x07;
            UINT32 bLastSet;
            b.GetLastSet(bLastSet);
            UINT32 bFirstSet;
            b.GetFirstSet(bFirstSet);
            end = bLastSet - bFirstSet + start;
            offset = b.offset;
            // Copy start to mask_len
            long startIndex = b.start >> 3;
            long endIndex = b.end >> 3;
            if (b.end < b.start)
            {
                ASSERT((b.mask_len - startIndex) <= mask_len);
                memcpy(mask, b.mask+startIndex, b.mask_len - startIndex);
                ASSERT((b.mask_len - startIndex + endIndex) <= mask_len);
                //memcpy(mask+b.mask_len-startIndex, b.mask, endIndex+1);  // old
                memcpy(mask+b.mask_len-startIndex, b.mask, endIndex);    // new
                // Clear any possible start/end overlap
                if (mask_len > b.mask_len)
                {
                    unsigned int remainder = start & 0x07;
                    if (remainder) mask[0] &= 0xff >> remainder;
                    remainder = end & 0x7;
                    if (remainder) 
                    {
                        ASSERT((startIndex+endIndex) < (long)mask_len);
                        mask[startIndex+endIndex] &= 0xff << (8 - remainder);
                    }
                }            
            }
            else
            {
                ASSERT((endIndex-startIndex+1) <= (long)mask_len);
                memcpy(mask, b.mask+startIndex, endIndex-startIndex+1);
            }        
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        Clear();
        return true;
    }
}  // end NormSlidingMask::Copy()

// Logically OR two bit mask such that "this = (this | b)"
bool NormSlidingMask::Add(const NormSlidingMask& b)
{
    if (b.IsSet())
    {
        if (IsSet())
        {
            UINT32 bFirstSet;
            b.GetFirstSet(bFirstSet);
            if (!CanSet(bFirstSet)) return false;
            UINT32 bLastSet;
            b.GetFirstSet(bLastSet);
            if (!CanSet(bLastSet)) return false;
            long range = b.end - b.start;
            if (range < 0) range += b.num_bits;
            unsigned long index;
            b.GetFirstSet(index);
            for (long i = 0; i < range; i++)
            {
                // (TBD) Improve performance by getting/setting
                //       ranges of set bits.
                if (b.Test(index)) Set(index);
                index++; 
            }
            return true;
        }
        else
        {
            return Copy(b);
        }
    }
    return true;
}  // end NormSlidingMask::Add()

// A sort of logical subtraction "this = (this & ~b)"
// This leaves us with bits uniquely set in "this" 
bool NormSlidingMask::Subtract(const NormSlidingMask& b)
{
    if (b.IsSet())
    {
        if (IsSet())
        {
            unsigned long index = offset;
            long range = end - start;
            if (range < 0) range += num_bits;
            for (long i = 0; i < range; i++)
            {
                if (Test(index) && b.Test(index)) Unset(index);
                index++;   
            }
        } 
    }
    return true;
}  // end NormSlidingMask::Subtract()

// A sort of logical subtraction "this = (~this & b)"
// This leaves us with bits uniquely set in "b"
bool NormSlidingMask::XCopy(const NormSlidingMask& b)
{
    if (b.IsSet())
    {
        if (IsSet())
        {
            UINT32 bFirstSet;
            b.GetFirstSet(bFirstSet);
            if (!CanSet(bFirstSet)) return false;
            UINT32 bLastSet;
            b.GetFirstSet(bLastSet);
            if (!CanSet(bLastSet)) return false;
            unsigned long index;
            b.GetFirstSet(index);
            long range = b.end - b.start;
            if (range < 0) range += b.num_bits;
            for (long i = 0; i < range; i++)
            {
                if (Test(index))
                    Unset(index);
                else if (b.Test(index))
                    Set(index);
                index++;   
            }
        }
        else
        {
            return Copy(b);   
        } 
    }
    else
    {
        Clear();
    }
    return true;
}  // end NormSlidingMask::XCopy()

// Logically AND two bit mask such that "this = (this & b)"
bool NormSlidingMask::Multiply(const NormSlidingMask& b)
{
    if (b.IsSet())
    {
        if (IsSet())
        {
            unsigned long index = offset;
            long range = end - start;
            if (range < 0) range += num_bits;
            for (long i = 0; i < range; i++)
            {
                if (Test(index) && !b.Test(index)) Unset(index);
                index++;   
            }
        } 
    }
    else
    {
        Clear();
    }
    return true;
}  // end NormSlidingMask::Multiply()

// Logically XOR two bit mask such that "this = (this ^ b)"
// (TBD) rewrite for byte-based operation for better efficiency
bool NormSlidingMask::Xor(const NormSlidingMask& b)
{
    if (b.IsSet())
    {
        UINT32 bFirstSet;
        b.GetFirstSet(bFirstSet);
        if (!CanSet(bFirstSet)) return false;
        UINT32 bLastSet;
        b.GetFirstSet(bLastSet);
        if (!CanSet(bLastSet)) return false;
        unsigned long index;
        b.GetFirstSet(index);
        long range = b.end - b.start;
        if (range < 0) range += b.num_bits;
        for (long i = 0; i < range; i++)
        {
            if (b.Test(index)) Invert(index);
            index++;   
        }
    }
    return true;
}  // end NormSlidingMask::Xor()

void NormSlidingMask::Display(FILE* stream)
{
    unsigned long index = offset;
    for (long i = 0; i < num_bits; i++)
    {
        if (Test(index++)) fprintf(stream, "1"); else fprintf(stream, "0");
        if (0x07 == (i & 0x07)) fprintf(stream, " ");
        if (0x3f == (i & 0x3f)) fprintf(stream, "\n");
    }
}  // end NormSlidingMask::Display()

void NormSlidingMask::Debug(long theCount)
{
    unsigned long index = offset;
    theCount = MIN(theCount, num_bits);
    DMSG(0, "NormSlidingMask::Debug() offset:%lu\n   ", index);
    long i;
    for (i = 0; i < theCount; i++)
    {
        if (Test(index++)) DMSG(0, "1"); else DMSG(0, "0");
        if (0x07 == (i & 0x07)) DMSG(0, " ");
        if (0x3f == (i & 0x3f)) DMSG(0, "\n   ");
    }
    if (0x3f != (i & 0x3f)) DMSG(0, "\n");
}  // end NormSlidingMask::Debug()
