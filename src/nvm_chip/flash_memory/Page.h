#ifndef PAGE_H
#define PAGE_H

#include "FlashTypes.h"

namespace NVM
{
	namespace FlashMemory
	{
		
		struct PageMetadata
		{
			//page_status_type Status;
			LPA_type LPA;
			uint8_t BAD;
		};

		class Page {
		public:
			Page()
			{
				//Metadata.Status = FREE_PAGE;
				Metadata.LPA = NO_LPA;
				Metadata.BAD = 0xff;
				//Metadata.SourceStreamID = NO_STREAM;
			};
			
			PageMetadata Metadata;

			void Write_metadata(const PageMetadata& metadata)
			{
				this->Metadata.LPA = metadata.LPA;
			}
			
			void Read_metadata(PageMetadata& metadata)
			{
				metadata.LPA = this->Metadata.LPA;
			}
			void Set_bad()
			{
				this->Metadata.BAD = 0x00; // Mark as bad
			}
			void Read_bad(uint8_t& bad)
			{
				bad = this->Metadata.BAD;
			}
		};
	}
}

#endif // !PAGE_H
