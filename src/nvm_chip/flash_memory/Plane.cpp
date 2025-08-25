#include "Plane.h"

namespace NVM
{
	namespace FlashMemory
	{
        void Plane::set_ramdom_bad_block()
        {
			for (unsigned int i = 0; i < bad_block_no; i++) {
				auto bad_block_id = rand() % Block_no;
				Blocks[bad_block_id]->Pages[0].Set_bad(); // Mark the first page as bad
				Blocks[bad_block_id]->Pages[Page_no - 1].Set_bad(); // Mark the last page as bad
				bad_block_ids.push_back(bad_block_id);
			}
        }

        Plane::Plane(unsigned int BlocksNoPerPlane, unsigned int PagesNoPerBlock, unsigned int BadBlockRatio) :
			Read_count(0), Progam_count(0), Erase_count(0)
		{
			Block_no = BlocksNoPerPlane;
			Page_no = PagesNoPerBlock;

			Bad_block_ratio = BadBlockRatio;
			bad_block_no = (int)(BlocksNoPerPlane * Bad_block_ratio / 100.0);
			Healthy_block_no = Block_no - bad_block_no;
			Blocks = new Block*[BlocksNoPerPlane];
			for (unsigned int i = 0; i < BlocksNoPerPlane; i++) {
				Blocks[i] = new Block(PagesNoPerBlock, i);
			}
			set_ramdom_bad_block();
			Allocated_streams = NULL;
			for (size_t i = 0; i < bad_block_ids.size(); ++i)
			{
				DEBUG_BIU("Bad block ID:" << bad_block_ids[i]);
			}
			
		}
		

		Plane::~Plane()
		{
			for (unsigned int i = 0; i < Healthy_block_no; i++) {
				delete Blocks[i];
			}
			delete[] Blocks;
		}
	}
}