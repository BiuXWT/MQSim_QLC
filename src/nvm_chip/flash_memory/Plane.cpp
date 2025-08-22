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
			}
        }

        Plane::Plane(unsigned int BlocksNoPerPlane, unsigned int PagesNoPerBlock, unsigned int BadBlockRatio) :
			Read_count(0), Progam_count(0), Erase_count(0)
		{
			Bad_block_ratio = BadBlockRatio;
			Block_no = BlocksNoPerPlane;
			Page_no = PagesNoPerBlock;
			Healthy_block_no = (int)(BlocksNoPerPlane * (100 - Bad_block_ratio) / 100.0);
			bad_block_no = BlocksNoPerPlane - Healthy_block_no;
			Blocks = new Block*[BlocksNoPerPlane];
			for (unsigned int i = 0; i < BlocksNoPerPlane; i++) {
				Blocks[i] = new Block(PagesNoPerBlock, i);
			}
			set_ramdom_bad_block();
			Allocated_streams = NULL;
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