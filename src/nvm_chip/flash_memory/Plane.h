#ifndef PLANE_H
#define PLANE_H

#include "../NVM_Types.h"
#include "FlashTypes.h"
#include "Block.h"
#include "Flash_Command.h"

namespace NVM
{
	namespace FlashMemory
	{
		class Plane
		{
		private:
			uint8_t Bad_block_ratio;
			void set_ramdom_bad_block();
			std::vector<int> bad_block_ids;

		public:
			Plane(unsigned int BlocksNoPerPlane, unsigned int PagesNoPerBlock, unsigned int BadBlockRatio);
			~Plane();
			Block** Blocks;
			unsigned int Block_no;
			unsigned int Page_no;
			unsigned int bad_block_no;
			unsigned int Healthy_block_no;
			unsigned long Read_count;                     //how many read count in the process of workload
			unsigned long Progam_count;
			unsigned long Erase_count;
			stream_id_type* Allocated_streams;
		};
	}
}
#endif // !PLANE_H
