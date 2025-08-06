#ifndef ADDRESS_MAPPING_UNIT_PAGE_LEVEL
#define ADDRESS_MAPPING_UNIT_PAGE_LEVEL

#include <unordered_map>
#include <map>
#include <queue>
#include <set>
#include <list>
#include "Address_Mapping_Unit_Base.h"
#include "Flash_Block_Manager_Base.h"
#include "SSD_Defs.h"
#include "NVM_Transaction_Flash_RD.h"
#include "NVM_Transaction_Flash_WR.h"
/**
 * LPA:逻辑页地址，是主机（Host）访问 SSD 时使用的逻辑地址。	作用：代表用户看到的连续逻辑存储空间中的页号。
 * PPA:物理页地址，是闪存芯片内部的物理地址。	作用：表示数据实际存储在闪存芯片中的位置（如：哪个 channel、chip、die、plane、block、page）
 * MVPN:虚拟翻译页号，是逻辑页地址（LPA）在翻译页表（GTD）中的索引。	作用：将 LPA 分组，每个 MVPN 对应一个翻译页（Translation Page），用于查找对应的 MPPN。
 *		举例：LPA = 1000，每翻译页包含 256 个逻辑页 → MVPN = 1000 / 256 = 3
 * MPPN:物理翻译页号，是翻译页在闪存中的物理地址。	作用：MVPN 到 MPPN 的映射由 GTD（Global Translation Directory）维护，用于快速定位翻译页在 Flash 中的物理位置。
 *		举例：MVPN = 3 → MPPN = 10000 → 表示翻译页 3 存储在物理页 10000 中。
 * 
 * 地址映射流程:LPA → MVPN → MPPN → Translation Page in Flash → PPA
 * LPA → MVPN:通过 get_MVPN(LPA, stream_id) 函数将 LPA 转换为 MVPN
 * MVPN → MPPN:使用 GlobalTranslationDirectory[MVPN] 查找对应的 MPPN。
 * MPPN → Translation Page Data:根据 MPPN 找到翻译页，翻译页中存储了多个 LPA 到 PPA 的映射
 * Translation Page → PPA:在翻译页中根据 LPA 找到其对应的 PPA
 * 
 */

namespace SSD_Components
{
#define MAKE_TABLE_INDEX(LPN,STREAM)

	enum class CMTEntryStatus {FREE, WAITING, VALID};

	struct GTDEntryType //用于全局翻译目录（Global Translation Directory）的条目，用于多级映射，记录mapping-table存储的page
	{
		MPPN_type MPPN;//物理翻译页号（MPPN），表示翻译页在 Flash 中的物理位置
		data_timestamp_type TimeStamp;
	};

	//缓存映射表的条目结构
	struct CMTSlotType
	{
		PPA_type PPA;//物理页地址
		unsigned long long WrittenStateBitmap;//页面写入状态位图，记录页面中哪些 sector 已写入。
		bool Dirty;//是否为“脏页”（即是否需要写回 Flash）
		CMTEntryStatus Status;//条目状态（FREE、WAITING、VALID）
		std::list<std::pair<LPA_type, CMTSlotType*>>::iterator listPtr;//本节点在LRU中的位置，用于快速实现LRU替换策略
		stream_id_type Stream_id;//所属流 ID，用于多流隔离
	};

	struct GMTEntryType//全局映射表（Global Mapping Table, GMT）的条目结构
	{
		PPA_type PPA;//物理页地址
		uint64_t WrittenStateBitmap;//页面写入状态位图，记录页面中哪些 sector 已写入
		data_timestamp_type TimeStamp;//时间戳，记录该条目的最后更新时间。
	};
	
	//缓存逻辑页到物理页的映射信息
	class Cached_Mapping_Table
	{
	public:
		Cached_Mapping_Table(unsigned int capacity);
		~Cached_Mapping_Table();
		bool Exists(const stream_id_type streamID, const LPA_type lpa);//检查某个 LPA 是否存在于缓存中
		// 获取 LPA 对应的 PPA;Retrieve:检索
		PPA_type Retrieve_ppa(const stream_id_type streamID, const LPA_type lpa);
		//更新缓存中的映射信息
		void Update_mapping_info(const stream_id_type streamID, const LPA_type lpa, const PPA_type ppa, const page_status_type pageWriteState);
		//插入新的映射信息
		void Insert_new_mapping_info(const stream_id_type streamID, const LPA_type lpa, const PPA_type ppa, const unsigned long long pageWriteState);
		page_status_type Get_bitmap_vector_of_written_sectors(const stream_id_type streamID, const LPA_type lpa);

		bool Is_slot_reserved_for_lpn_and_waiting(const stream_id_type streamID, const LPA_type lpa);
		bool Check_free_slot_availability();
		void Reserve_slot_for_lpn(const stream_id_type streamID, const LPA_type lpa);
		//驱逐一个缓存条目（用于腾出空间）
		CMTSlotType Evict_one_slot(LPA_type& lpa);
		//判断缓存条目的“脏”状态。
		bool Is_dirty(const stream_id_type streamID, const LPA_type lpa);
		//清除缓存条目的“脏”状态。
		void Make_clean(const stream_id_type streamID, const LPA_type lpa);
	private:
		// 地址映射：存储逻辑页号到物理页号的映射---哈希表，用于快速查找 LPA 到 CMTSlot 的映射
		std::unordered_map<LPA_type, CMTSlotType*> addressMap; 
		//LRU 链表，用于管理缓存条目的替换策略。
		std::list<std::pair<LPA_type, CMTSlotType*>> lruList;  
		//CMT 的容量
		unsigned int capacity;
	};

	/* Each stream has its own address mapping domain. It helps isolation of GC interference
	* (e.g., multi-streamed SSD HotStorage 2014, and OPS isolation in FAST 2015)
	* However, CMT is shared among concurrent streams in two ways: 1) each address mapping domain
	* shares the whole CMT space with other domains, and 2) each address mapping domain has
	* its own share of CMT (equal partitioning of CMT space among concurrent streams).
	  每个流都有其自己的地址映射域。这有助于隔离垃圾回收干扰
	*（例如，多流 SSD HotStorage 2014 和 FAST 2015 中的 OPS 隔离）
	* 然而，CMT 在并发流之间以两种方式共享：
		1) 每个地址映射域与其他域共享整个 CMT 空间，
		2) 每个地址映射域有其自己的一部分 CMT（在并发流之间均等划分 CMT 空间）。
	*/

	//表示一个地址映射域，用于隔离多个流的地址映射操作
	class AddressMappingDomain
	{
	public:
		AddressMappingDomain(unsigned int cmt_capacity, unsigned int cmt_entry_size, unsigned int no_of_translation_entries_per_page,
			Cached_Mapping_Table* CMT,
			Flash_Plane_Allocation_Scheme_Type PlaneAllocationScheme,
			flash_channel_ID_type* channel_ids, unsigned int channel_no, flash_chip_ID_type* chip_ids, unsigned int chip_no,
			flash_die_ID_type* die_ids, unsigned int die_no, flash_plane_ID_type* plane_ids, unsigned int plane_no,
			PPA_type total_physical_sectors_no, LHA_type total_logical_sectors_no, unsigned int sectors_no_per_page);
		~AddressMappingDomain();

		/*Stores the mapping of Virtual Translation Page Number (MVPN) to Physical Translation Page Number (MPPN).
		* It is always kept in volatile memory.
		存储虚拟页号 (MVPN) 到物理页号 (MPPN) 的映射。它始终保存在易失性内存中。*/
		GTDEntryType* GlobalTranslationDirectory; //全局翻译目录（GTD），存储 MVPN(虚拟页号) 到 MPPN（物理页号） 的映射

		/*The cached mapping table that is implemented based on the DFLT (Gupta et al., ASPLOS 2009) proposal.
		* It is always stored in volatile memory.
		基于DFLT（Gupta等，ASPLOS 2009）提案实现的缓存映射表。它始终存储在易失性内存中。*/
		unsigned int CMT_entry_size;
		unsigned int Translation_entries_per_page;
		//缓存的LPA->PPA映射表；存储在dram中
		Cached_Mapping_Table* CMT;
		unsigned int No_of_inserted_entries_in_preconditioning;

		/*The logical to physical address mapping of all data pages that is implemented based on the DFTL (Gupta et al., ASPLOS 2009(
		* proposal. It is always stored in non-volatile flash memory.
		基于DFTL（Gupta等，ASPLOS 2009）的所有数据页面的逻辑到物理地址映射。这些映射总是存储在非易失性闪存中。*/
		// 全局(全量)的LPA->PPA映射表（GMT）;存储在flash中
		GMTEntryType *GlobalMappingTable;
		void Update_mapping_info(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa, const PPA_type ppa, const page_status_type page_status_bitmap);
		//获取页面写入状态位图，记录了页面中哪些 sector 已写入
		page_status_type Get_page_status(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa);
		//获取LPA对应的ppa
		PPA_type Get_ppa(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa);
		PPA_type Get_ppa_for_preconditioning(const stream_id_type stream_id, const LPA_type lpa);
		bool Mapping_entry_accessible(const bool ideal_mapping, const stream_id_type stream_id, const LPA_type lpa);
		MVPN_type get_MVPN(const LPA_type lpn, stream_id_type stream_id=0) const;

		std::multimap<LPA_type, NVM_Transaction_Flash*> Waiting_unmapped_read_transactions;
		std::multimap<LPA_type, NVM_Transaction_Flash*> Waiting_unmapped_program_transactions;
		std::multimap<MVPN_type, LPA_type> ArrivingMappingEntries;
		std::set<MVPN_type> DepartingMappingEntries;
		std::set<LPA_type> Locked_LPAs;//用于管理竞争条件，即用户请求在垃圾回收（GC）移动该逻辑页面（LPA）时访问该LPA 
		std::set<MVPN_type> Locked_MVPNs;//用于管理竞争条件
		std::multimap<LPA_type, NVM_Transaction_Flash*> Read_transactions_behind_LPA_barrier;
		std::multimap<LPA_type, NVM_Transaction_Flash*> Write_transactions_behind_LPA_barrier;
		std::set<MVPN_type> MVPN_read_transactions_waiting_behind_barrier;
		std::set<MVPN_type> MVPN_write_transaction_waiting_behind_barrier;

		Flash_Plane_Allocation_Scheme_Type PlaneAllocationScheme;
		flash_channel_ID_type* Channel_ids;
		unsigned int Channel_no;
		flash_chip_ID_type* Chip_ids;
		unsigned int Chip_no;
		flash_die_ID_type* Die_ids;
		unsigned int Die_no;
		flash_plane_ID_type* Plane_ids;
		unsigned int Plane_no;

		LHA_type max_logical_sector_address;
		LPA_type Total_logical_pages_no;
		PPA_type Total_physical_pages_no;
		MVPN_type Total_translation_pages_no;
	};

	class Address_Mapping_Unit_Page_Level : public Address_Mapping_Unit_Base
	{
		friend class GC_and_WL_Unit_Page_Level;
	public:
		Address_Mapping_Unit_Page_Level(const sim_object_id_type& id, FTL* ftl, NVM_PHY_ONFI* flash_controller, Flash_Block_Manager_Base* block_manager,
			bool ideal_mapping_table, unsigned int cmt_capacity_in_byte, Flash_Plane_Allocation_Scheme_Type PlaneAllocationScheme,
			unsigned int ConcurrentStreamNo,
			unsigned int ChannelCount, unsigned int chip_no_per_channel, unsigned int DieNoPerChip, unsigned int PlaneNoPerDie,
			std::vector<std::vector<flash_channel_ID_type>> stream_channel_ids, std::vector<std::vector<flash_chip_ID_type>> stream_chip_ids,
			std::vector<std::vector<flash_die_ID_type>> stream_die_ids, std::vector<std::vector<flash_plane_ID_type>> stream_plane_ids,
			unsigned int Block_no_per_plane, unsigned int Page_no_per_block, unsigned int SectorsPerPage, unsigned int PageSizeInBytes,
			double Overprovisioning_ratio, CMT_Sharing_Mode sharing_mode = CMT_Sharing_Mode::SHARED, bool fold_large_addresses = true);
		~Address_Mapping_Unit_Page_Level();
		void Setup_triggers();
		void Start_simulation();
		void Validate_simulation_config();
		void Execute_simulator_event(MQSimEngine::Sim_Event*);

		//用于预处理的函数
		void Allocate_address_for_preconditioning(const stream_id_type stream_id, std::map<LPA_type, page_status_type>& lpa_list, std::vector<double>& steady_state_distribution);
		int Bring_to_CMT_for_preconditioning(stream_id_type stream_id, LPA_type lpa);
		void Store_mapping_table_on_flash_at_start();
		unsigned int Get_cmt_capacity();
		unsigned int Get_current_cmt_occupancy_for_stream(stream_id_type stream_id);
		
		//地址翻译函数
		void Translate_lpa_to_ppa_and_dispatch(const std::list<NVM_Transaction*>& transactionList);
		void Get_data_mapping_info_for_gc(const stream_id_type stream_id, const LPA_type lpa, PPA_type& ppa, page_status_type& page_state);
		void Get_translation_mapping_info_for_gc(const stream_id_type stream_id, const MVPN_type mvpn, MPPN_type& mppa, sim_time_type& timestamp);
		void Allocate_new_page_for_gc(NVM_Transaction_Flash_WR* transaction, bool is_translation_page);
		LPA_type Get_logical_pages_count(stream_id_type stream_id);
		NVM::FlashMemory::Physical_Page_Address Convert_ppa_to_address(const PPA_type ppa);
		//转换ppa为Physical_Page_Address结构
		void Convert_ppa_to_address(const PPA_type ppn, NVM::FlashMemory::Physical_Page_Address& address);
		PPA_type Convert_address_to_ppa(const NVM::FlashMemory::Physical_Page_Address& pageAddress);
		
		// 垃圾回收和磨损均衡执行的系统状态一致性控制函数
		void Set_barrier_for_accessing_physical_block(const NVM::FlashMemory::Physical_Page_Address& block_address);
		void Set_barrier_for_accessing_lpa(stream_id_type stream_id, LPA_type lpa);
		void Set_barrier_for_accessing_mvpn(stream_id_type stream_id, MVPN_type mpvn);
		void Remove_barrier_for_accessing_lpa(stream_id_type stream_id, LPA_type lpa);
		void Remove_barrier_for_accessing_mvpn(stream_id_type stream_id, MVPN_type mpvn);
		void Start_servicing_writes_for_overfull_plane(const NVM::FlashMemory::Physical_Page_Address plane_address);
	private:
		static Address_Mapping_Unit_Page_Level* _my_instance;
		unsigned int cmt_capacity;//cmt条目数
		AddressMappingDomain** domains;//地址映射域，按stream区分，记录CMT、GMT、GTD
		//CMT cached mapping table;GTD Global Translation Directory 虚拟页号 (MVPN) 到物理页号 (MPPN) 的映射
		unsigned int CMT_entry_size, GTD_entry_size;//In CMT MQSim stores (lpn, ppn, page status bits) but in GTD it only stores (ppn, page status bits) //在CMT中，MQSim存储（lpn，ppn，页面状态位），而在GTD中仅存储（ppn，页面状态位）
		//从LPA计算得到plane的 Physical_Page_Address 地址,只计算到plane层
		void allocate_plane_for_user_write(NVM_Transaction_Flash_WR* transaction);
		void allocate_page_in_plane_for_user_write(NVM_Transaction_Flash_WR* transaction, bool is_for_gc);//从plane中分配page用于写事务
		void allocate_plane_for_translation_write(NVM_Transaction_Flash* transaction);
		void allocate_page_in_plane_for_translation_write(NVM_Transaction_Flash* transaction, MVPN_type mvpn, bool is_for_gc);
		void allocate_plane_for_preconditioning(stream_id_type stream_id, LPA_type lpn, NVM::FlashMemory::Physical_Page_Address& targetAddress);
		bool request_mapping_entry(const stream_id_type streamID, const LPA_type lpn);
		static void handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* transaction);
		bool translate_lpa_to_ppa(stream_id_type streamID, NVM_Transaction_Flash* transaction);
		// 当某个 plane 没有足够的空闲页（free pages）来满足当前写请求时，该写请求会被暂时挂起并插入到 Write_transactions_for_overfull_planes 对应的位置中，等待后续 GC（垃圾回收）释放空间后，再重新尝试写入。
		// [channel][chip][die][plane] 4级指针, 存储的元素是NVM_Transaction_Flash_WR*
		std::set<NVM_Transaction_Flash_WR*>**** Write_transactions_for_overfull_planes;

		void generate_flash_read_request_for_mapping_data(const stream_id_type streamID, const LPA_type lpn);
		void generate_flash_writeback_request_for_mapping_data(const stream_id_type streamID, const LPA_type lpn);

		unsigned int no_of_translation_entries_per_page;
		MVPN_type get_MVPN(const LPA_type lpn, stream_id_type stream_id);//mapping of Virtual Translation Page Number (MVPN)
		LPA_type get_start_LPN_in_MVP(const MVPN_type);
		LPA_type get_end_LPN_in_MVP(const MVPN_type);

		//查询cached mapping table
		bool query_cmt(NVM_Transaction_Flash* transaction);
		//在读操作过程中动态创建缓存映射表（CMT）条目
		PPA_type online_create_entry_for_reads(LPA_type lpa, const stream_id_type stream_id, NVM::FlashMemory::Physical_Page_Address& read_address, uint64_t read_sectors_bitmap);
		//把事务加到Write_transactions_for_overfull_planes，等待gc
		void manage_unsuccessful_translation(NVM_Transaction_Flash* transaction);
		//用户事务遇到屏障，则添加到Read/Write_transactions_behind_LPA_barrier集合中，稍后处理
		void manage_user_transaction_facing_barrier(NVM_Transaction_Flash* transaction);
		//映射事务遇到屏障，则添加到MVPN_read/write_transactions_waiting_behind_barrier集合中，稍后处理
		void manage_mapping_transaction_facing_barrier(stream_id_type stream_id, MVPN_type mvpn, bool read);
		bool is_lpa_locked_for_gc(stream_id_type stream_id, LPA_type lpa);
		bool is_mvpn_locked_for_gc(stream_id_type stream_id, MVPN_type mvpn);
	};

}

#endif // !ADDRESS_MAPPING_UNIT_PAGE_LEVEL
