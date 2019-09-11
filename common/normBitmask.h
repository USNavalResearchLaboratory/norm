#ifndef _NORM_BITMASK_
#define _NORM_BITMASK_

#include "debug.h" // for PROTO_DEBUG stuff
#include <string.h>  // for memset()
#include <stdio.h>   // for fprintf()


/***************************************************************
 * This class also provides space-efficient binary storage.
 * It's pretty much just a flat-indexed array of bits, but
 * keeps some state to be relatively efficient for various
 * operations.
 */

class NormBitmask
{    
    // Methods
    public:
        NormBitmask();
        ~NormBitmask();
        
        bool Init(unsigned long numBits);
        void Destroy();
        unsigned long Size() {return num_bits;}
        void Clear()  // set to all zero's
        {
            memset(mask, 0, mask_len);
            first_set = num_bits;
        };
        void Reset()  // set to all one's
        {
            memset(mask, 0xff, (mask_len-1));
            mask[mask_len-1] = 0x00ff << ((8 - (num_bits & 0x07)) & 0x07);
            first_set = 0;
        }
        
        bool IsSet() const {return (first_set < num_bits);}
        unsigned long FirstSet() const {return first_set;}
        unsigned long LastSet() const
        {
            return ((first_set < num_bits) ?
                     PrevSet(num_bits - 1) : num_bits);   
        }
        bool Test(unsigned long index) const
        {
            return ((index < num_bits) ?
			        (0 !=  (mask[(index >> 3)] & (0x80 >> (index & 0x07)))) :
                    false);
        }
        bool CanSet(unsigned long index) const
            {return (index < num_bits);}
        
        bool Set(unsigned long index)
        {
            if (index < num_bits)
            {
                mask[(index >> 3)] |= (0x80 >> (index & 0x07));
                (index < first_set) ? (first_set = index) : 0;
                return true;
            }
            else
            {
                return false;
            }
        }
        bool Unset(unsigned long index)
        {
            if (index < num_bits)
            {
                mask[(index >> 3)] &= ~(0x80 >> (index & 0x07));
                (index == first_set) ? first_set = NextSet(index) : 0;
            }
            return true;
        }
        bool  Invert(unsigned long index)
            {return (Test(index) ? Unset(index) : Set(index));}
        
        bool SetBits(unsigned long baseIndex, unsigned long count);
        bool UnsetBits(unsigned long baseIndex, unsigned long count);
        
        unsigned long NextSet(unsigned long index) const;
        unsigned long PrevSet(unsigned long index) const;
        unsigned long NextUnset(unsigned long index) const;
        
        bool Copy(const NormBitmask &b);        // this = b
        bool Add(const NormBitmask & b);        // this = this | b
        bool Subtract(const NormBitmask & b);   // this = this & ~b
        bool XCopy(const NormBitmask & b);      // this = ~this & b
        bool Multiply(const NormBitmask & b);   // this = this & b
        bool Xor(const NormBitmask & b);        // this = this ^ b
        
        void Display(FILE* stream);
        
    // Members
    private:
        unsigned char*  mask;
        unsigned long   mask_len;
        unsigned long   num_bits;
        unsigned long   first_set;  // index of lowest _set_ bit
};  // end class NormBitmask



/***************************************************************
 * This class also provides space-efficient binary storage.
 * More than just a flat-indexed array of bits, this
 * class can also automatically act as a sliding
 * window buffer as long as the range of set bit
 * indexes fall within the number of storage bits
 * for which the class initialized.
 */
 
class NormSlidingMask
{
    public:
        NormSlidingMask();
        ~NormSlidingMask();
        
        bool Init(long numBits);
        void Destroy();
        long Size() const {return num_bits;}
        void Clear()
        {
            memset(mask, 0, mask_len); 
            start = end = num_bits; 
            offset = 0; 
        }
        void Reset(unsigned long index = 0)
        {
            ASSERT(num_bits);
            memset(mask, 0xff, mask_len);
            mask[mask_len-1] = 0x00ff << ((8 - (num_bits & 0x07)) & 0x07);
            start = 0;
            end = num_bits - 1;
            offset = index;
        }
        bool IsSet() const {return (start < num_bits);}        
        unsigned long FirstSet() const {return offset;}
        unsigned long LastSet() const
        {
            long n = end - start;
            n = (n < 0) ? (n + num_bits) : n;
            return (n + offset); 
        }
        bool Test(unsigned long index) const;
        bool CanSet(unsigned long index) const;
        
        bool Set(unsigned long index);
        bool Unset(unsigned long index);
        bool Invert(unsigned long index)
            {return (Test(index) ? Unset(index): Set(index));}   
        
        bool SetBits(unsigned long index, long count);
        bool UnsetBits(unsigned long index, long count);
                
        
        
        // These return "FirstSet()+Size()" when finding nothing
        unsigned long NextSet(unsigned long index) const;
        unsigned long PrevSet(unsigned long index) const;
        
        static unsigned long RawNextSet(const char* mask, long index, long start);
        static unsigned long RawPrevSet(const char* mask, long index, long end);
        
        
        bool Copy(const NormSlidingMask& b);        // this = b
        bool Add(const NormSlidingMask & b);        // this = this | b
        bool Subtract(const NormSlidingMask & b);   // this = this & ~b
        bool XCopy(const NormSlidingMask & b);      // this = ~this & b
        bool Multiply(const NormSlidingMask & b);   // this = this & b
        bool Xor(const NormSlidingMask & b);        // this = this ^ b
        
        void Display(FILE* stream);
        void Debug(long theCount);
        
    private:
        unsigned char*  mask;
        unsigned long   mask_len;
        long            num_bits;
        long            start;
        long            end;
        unsigned long   offset;
};  // end class NormSlidingMask

#endif // _NORM_BITMASK_
