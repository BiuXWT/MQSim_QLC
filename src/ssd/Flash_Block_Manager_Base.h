#ifndef BLOCK_POOL_MANAGER_BASE_H
#define BLOCK_POOL_MANAGER_BASE_H

#include <list>
#include <cstdint>
#include <queue>
#include <set>
#include "../nvm_chip/flash_memory/FlashTypes.h"
#include "../nvm_chip/flash_memory/Physical_Page_Address.h"
#include "GC_and_WL_Unit_Base.h"

namespace SSD_Components
{
#define All_VALID_PAGE 0x0000000000000000ULL
	class GC_and_WL_Unit_Base;
	/*
	* Block_Service_Status is used to impelement a state machine for each physical block in order to
	* eliminate race conditions between GC page movements and normal user I/O requests.
	* Allowed transitions:
	* Block_Service_Status用于实现每个物理块的状态机，以消除GC页面移动和正常用户I/O请求之间的竞争条件。* 允许的状态转换：
	* 1: IDLE -> GC, IDLE -> USER
	* 2: GC -> IDLE, GC -> GC_UWAIT
	* 3: USER -> IDLE, USER -> GC_USER
	* 4: GC_UWAIT -> GC, GC_UWAIT -> GC_UWAIT
	* 5: GC_USER -> GC
	*/
	enum class Block_Service_Status {IDLE, GC_WL, USER, GC_USER, GC_UWAIT, GC_USER_UWAIT};
	
	class Block_Pool_Slot_Type
	{
	public:
	//基本属性
		flash_block_ID_type BlockID;
		//当前写入页索引
		flash_page_ID_type Current_page_write_index;
		//管理块的状态,防止 GC 与用户 I/O 请求之间的竞争条件
		Block_Service_Status Current_status;
		bool bad_block=false;//标记是否为坏块
	//页面有效性管理
		unsigned int Invalid_page_count;//无效页计数
		//静态变量，表示每个块中页面数量对应的位图大小（以 uint64_t 为单位）
		static unsigned int Page_vector_size;
		uint64_t* Invalid_page_bitmap;//跟踪块中页面的有效/无效状态的位序列。“0”表示有效，“1”表示无效。
	//擦除管理
		//该块已经被擦除的次数，用于磨损均衡（Wear Leveling）策略中判断块的寿命
		unsigned int Erase_count;
		//当前正在进行的擦除事务（如果有的话），用于跟踪擦除操作的进度
		NVM_Transaction_Flash_ER* Erase_transaction;

	//stream与数据类型管理
		stream_id_type Stream_id = NO_STREAM;
		bool Holds_mapping_data = false;
		bool Hot_block = false;//用于在 "关于热数据和冷数据识别的重要性以减少基于闪存的SSD中的写入放大" 中提到的热/冷分离，性能评估，2014年。
	//并发控制
		bool Has_ongoing_gc_wl = false;//块是否正在被垃圾回收
		int Ongoing_user_read_count;//当前正在进行的用户读操作数
		int Ongoing_user_program_count;//当前正在进行的用户写操作数
		void Erase();
	};

	//管理SSD中一个Plane的Block使用情况和垃圾回收相关信息 :book keeping(记账，簿记，管账)
	class PlaneBookKeepingType
	{
	public:
		//统计信息,用于跟踪 Plane 中页的使用状态，便于垃圾回收和空间管理
		unsigned int Total_pages_count;		//当前 Plane 中总页数
		unsigned int Free_pages_count;		//当前 Plane 中空闲页数
		unsigned int Valid_pages_count;		//当前 Plane 中有效页数
		unsigned int Invalid_pages_count;	//当前 Plane 中无效页数
		//块管理
		Block_Pool_Slot_Type* Blocks;
		Block_Pool_Slot_Type *Bad_Blocks;
		std::multimap<unsigned int, Block_Pool_Slot_Type*> Free_block_pool;//用于存储当前 Plane 中可用的空闲块<Erase_count,Block>
		//写入前沿，即open-block，当前正在被写入的Block
		//The write frontier blocks for data and GC pages. MQSim adopts Double Write Frontier approach for user and GC writes which is shown very advantages in: B. Van Houdt, "On the necessity of hot and cold data identification to reduce the write amplification in flash - based SSDs", Perf. Eval., 2014
		Block_Pool_Slot_Type **Data_wf, **GC_wf; // Data Write Frontier指向当前用于数据当前写入的目标块;GC Write Frontier指向GC当前写入的目标块
		Block_Pool_Slot_Type** Translation_wf; //The write frontier blocks for translation GC pages翻译页的写入前沿(open-block)，用于维护地址映射信息
		//使用历史记录
		std::queue<flash_block_ID_type> Block_usage_history;//使用 FIFO（先进先出）队列记录块的使用历史
		//正在进行擦除的块
		std::set<flash_block_ID_type> Ongoing_erase_operations;//防止并发操作冲突，确保擦除完成后再进行其他操作
		Block_Pool_Slot_Type* Get_a_free_block(stream_id_type stream_id, bool for_mapping_data);
		unsigned int Get_free_block_pool_size();
		void Check_bookkeeping_correctness(const NVM::FlashMemory::Physical_Page_Address& plane_address);//检查类中统计信息（如总页数、空闲页数、有效页数和无效页数）是否一致
		void Add_to_free_block_pool(Block_Pool_Slot_Type* block, bool consider_dynamic_wl);
	};

	class Flash_Block_Manager_Base
	{
		friend class Address_Mapping_Unit_Page_Level;
		friend class GC_and_WL_Unit_Page_Level;
		friend class GC_and_WL_Unit_Base;
	public:
		Flash_Block_Manager_Base(GC_and_WL_Unit_Base* gc_and_wl_unit, unsigned int max_allowed_block_erase_count, unsigned int total_concurrent_streams_no,
			unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
			unsigned int block_no_per_plane, unsigned int page_no_per_block);
		virtual ~Flash_Block_Manager_Base();
		//为用户写操作分配一个块/页
		virtual void Allocate_block_and_page_in_plane_for_user_write(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address) = 0;
		//为用GC操作分配一个块/页
		virtual void Allocate_block_and_page_in_plane_for_gc_write(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address) = 0;
		//为地址翻译写入操作分配一个块/页
		virtual void Allocate_block_and_page_in_plane_for_translation_write(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address, bool is_for_gc) = 0;
		virtual void Allocate_Pages_in_block_and_invalidate_remaining_for_preconditioning(const stream_id_type stream_id, const NVM::FlashMemory::Physical_Page_Address& plane_address, std::vector<NVM::FlashMemory::Physical_Page_Address>& page_addresses) = 0;
		//将指定块中的某一页标记为无效
		virtual void Invalidate_page_in_block(const stream_id_type streamID, const NVM::FlashMemory::Physical_Page_Address& address) = 0;
		virtual void Invalidate_page_in_block_for_preconditioning(const stream_id_type streamID, const NVM::FlashMemory::Physical_Page_Address& address) = 0;
		//将擦除后的块加入空闲块池
		virtual void Add_erased_block_to_pool(const NVM::FlashMemory::Physical_Page_Address& address) = 0;
		//获取指定plane中空闲块池的大小
		virtual unsigned int Get_pool_size(const NVM::FlashMemory::Physical_Page_Address& plane_address) = 0;


		flash_block_ID_type Get_coldest_block_id(const NVM::FlashMemory::Physical_Page_Address& plane_address);
		unsigned int Get_min_max_erase_difference(const NVM::FlashMemory::Physical_Page_Address& plane_address);//最热和最冷block的 P/E cycle 之差
		void Set_GC_and_WL_Unit(GC_and_WL_Unit_Base* );
		PlaneBookKeepingType* Get_plane_bookkeeping_entry(const NVM::FlashMemory::Physical_Page_Address& plane_address);//从plane_manager中获取当前plane的PBK
		bool Block_has_ongoing_gc_wl(const NVM::FlashMemory::Physical_Page_Address& block_address);//检查当前block是否正在执行GC_WL
		bool Can_execute_gc_wl(const NVM::FlashMemory::Physical_Page_Address& block_address);//检查 gc 请求是否可以在 block_address 上执行（不应有任何针对 block_address 的持续用户读取/程序请求）
		////标记当前块在执行GC_WL Has_ongoing_gc_wl true
		void GC_WL_started(const NVM::FlashMemory::Physical_Page_Address& block_address);
		//标记当前块已完成GC_WL:Has_ongoing_gc_wl false
		void GC_WL_finished(const NVM::FlashMemory::Physical_Page_Address& block_address);
		//标记当前块读事务正在进行：Ongoing_user_read_count++
		void Read_transaction_issued(const NVM::FlashMemory::Physical_Page_Address& page_address);
		//标记当前块读事务已完成：Ongoing_user_read_count--
		void Read_transaction_serviced(const NVM::FlashMemory::Physical_Page_Address& page_address);
		//标记当前块读事务已完成：Ongoing_user_program_count--
		void Program_transaction_serviced(const NVM::FlashMemory::Physical_Page_Address& page_address);
		//检查是否有正在进行的写入操作：Ongoing_user_program_count > 0
		bool Is_having_ongoing_program(const NVM::FlashMemory::Physical_Page_Address& block_address);
		bool Is_page_valid(Block_Pool_Slot_Type* block, flash_page_ID_type page_id);//检查page状态
	protected:
		//所有plane中block的使用情况 [channel] [chip] [die] [plane]：4维数组
		PlaneBookKeepingType ****plane_manager;
		GC_and_WL_Unit_Base *gc_and_wl_unit;
		unsigned int max_allowed_block_erase_count;//PE cycle
		unsigned int total_concurrent_streams_no;
		unsigned int channel_count;
		unsigned int chip_no_per_channel;
		unsigned int die_no_per_chip;
		unsigned int plane_no_per_die;
		unsigned int block_no_per_plane;
		unsigned int pages_no_per_block;
		void program_transaction_issued(const NVM::FlashMemory::Physical_Page_Address& page_address);//Updates the block bookkeeping record
	};
}

#endif//!BLOCK_POOL_MANAGER_BASE_H
