#include "Physical_Page_Address.h"

namespace NVM
{
	namespace FlashMemory
	{
		bool Physical_Page_Address::block_address_constraint_for_multiplane = true;
		std::ostream& operator<<(std::ostream& os, Physical_Page_Address& addr)
		{
			os << addr.ChannelID << "-" << addr.ChipID << "-" << addr.DieID << "-" << addr.PlaneID << "-" << addr.BlockID << "-" << addr.PageID;
			return os;
		}
	}
}