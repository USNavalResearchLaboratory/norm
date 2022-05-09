#include "grttProbe.h"

// This code is _not_ part of the NORM protocol library. This
// is an independent class that can be used as an alternative
// to conduct the sort of GRTT probing that is conducted as part 
// of NORM protocol operation.  This can be used in conjunction
// with NORM by disabling NORM's built-in GRTT probing and using  
// the information collected from this independent probing  to 
// dynamically and explicitly set the NORM sender GRTT estimate.


using namespace GrttProbe
{
    // return length in 32-bit words (i.e. 1 word == 4 bytes)
    unsigned int GetIdWords(IdType idType)
    {
        switch (idType)
        {
            case ID_INT32:
            case ID_IPV4:
                return 1;
            case ID_IPV6:
                return 4;
            case ID_ETH:
                return 2;  // includes padding
            default:
                return 0;
        }
    }  // end GrttProbe::Message::GetIdWords()
};  // end namespace GrttProbe
