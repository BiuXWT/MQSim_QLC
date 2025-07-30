#ifndef ADDRESS_MAPPING_UNIT_BASE_H
#define ADDRESS_MAPPING_UNIT_BASE_H

#include "../sim/Sim_Object.h"
#include "../nvm_chip/flash_memory/Physical_Page_Address.h"
#include "../nvm_chip/flash_memory/FlashTypes.h"
#include "SSD_Defs.h"
#include "NVM_Transaction_Flash.h"
#include "NVM_PHY_ONFI_NVDDR2.h"
#include "FTL.h"
#include "Flash_Block_Manager_Base.h"

namespace SSD_Components
{
	class FTL;
	class Flash_Block_Manager_Base;

	typedef uint32_t MVPN_type;
	typedef uint32_t MPPN_type;

	enum class Flash_Address_Mapping_Type {PAGE_LEVEL, HYBRID};
	enum class Flash_Plane_Allocation_Scheme_Type
	{
		CWDP, CWPD, CDWP, CDPW, CPWD, CPDW,
		WCDP, WCPD, WDCP, WDPC, WPCD, WPDC,
		DCWP, DCPW, DWCP, DWPC, DPCW, DPWC,
		PCWD, PCDW, PWCD, PWDC, PDCW, PDWC
	};
	enum class CMT_Sharing_Mode { SHARED, EQUAL_SIZE_PARTITIONING };

	enum class Moving_LPA_Status { GC_IS_READING_PHYSICAL_BLOCK, GC_IS_READING_DATA, GC_IS_WRITING_DATA, 
		GC_IS_READING_PHYSICAL_BLOCK_AND_THERE_IS_USER_READ, GC_IS_READING_DATA_AND_THERE_IS_USER_READ,
	    GC_IS_READING_PHYSICAL_BLOCK_AND_PAGE_IS_INVALIDATED, GC_IS_READING_DATA_AND_PAGE_IS_INVALIDATED, GC_IS_WRITING_DATA_AND_PAGE_IS_INVALIDATED};

	class Address_Mapping_Unit_Base : public MQSimEngine::Sim_Object
	{
	public:
		Address_Mapping_Unit_Base(const sim_object_id_type& id, FTL* ftl, NVM_PHY_ONFI* FlashController, Flash_Block_Manager_Base* block_manager,
			bool ideal_mapping_table, unsigned int no_of_input_streams,
			unsigned int ChannelCount, unsigned int chip_no_per_channel, unsigned int DieNoPerChip, unsigned int PlaneNoPerDie,
			unsigned int Block_no_per_plane, unsigned int Page_no_per_block, unsigned int SectorsPerPage, unsigned int PageSizeInBytes,
			double Overprovisioning_ratio, CMT_Sharing_Mode sharing_mode = CMT_Sharing_Mode::SHARED, bool fold_large_addresses = true);
		virtual ~Address_Mapping_Unit_Base();

		//用于预处理的功能
		virtual void Allocate_address_for_preconditioning(const stream_id_type stream_id, std::map<LPA_type, page_status_type>& lpa_list, std::vector<double>& steady_state_distribution) = 0;
		virtual int Bring_to_CMT_for_preconditioning(stream_id_type stream_id, LPA_type lpa) = 0;//用于在预处理期间加热缓存映射表
		virtual void Store_mapping_table_on_flash_at_start() = 0; //它应该仅在模拟开始时调用，以便在闪存空间中存储映射表条目。

		
		virtual unsigned int Get_cmt_capacity() = 0;//返回可以存储在缓存映射表中的最大条目数
		virtual unsigned int Get_current_cmt_occupancy_for_stream(stream_id_type stream_id) = 0;
		virtual LPA_type Get_logical_pages_count(stream_id_type stream_id) = 0; //返回分配给 I/O 流的逻辑页数
		unsigned int Get_no_of_input_streams() { return no_of_input_streams; }
		bool Is_ideal_mapping_table(); //检查理想映射表是否启用，其中所有地址翻译条目始终在 CMT 中（即，CMT 的大小是无限的），因此所有地址翻译请求总是成功的。

		//Address translation functions
		virtual void Translate_lpa_to_ppa_and_dispatch(const std::list<NVM_Transaction*>& transactionList) = 0;
		virtual void Get_data_mapping_info_for_gc(const stream_id_type stream_id, const LPA_type lpa, PPA_type& ppa, page_status_type& page_state) = 0;
		virtual void Get_translation_mapping_info_for_gc(const stream_id_type stream_id, const MVPN_type mvpn, MPPN_type& mppa, sim_time_type& timestamp) = 0;
		virtual void Allocate_new_page_for_gc(NVM_Transaction_Flash_WR* transaction, bool is_translation_page) = 0;
		unsigned int Get_device_physical_pages_count();//Returns the number of physical pages in the device
		CMT_Sharing_Mode Get_CMT_sharing_mode();
		virtual NVM::FlashMemory::Physical_Page_Address Convert_ppa_to_address(const PPA_type ppa) = 0;
		virtual void Convert_ppa_to_address(const PPA_type ppa, NVM::FlashMemory::Physical_Page_Address& address) = 0;
		virtual PPA_type Convert_address_to_ppa(const NVM::FlashMemory::Physical_Page_Address& pageAddress) = 0;

		/*********************************************************************************************************************
		 These are system state consistency control functions that are used for garbage collection and wear-leveling execution.
		 Once the GC_and_WL_Unit_Base starts moving a logical page (LPA) from one physical location to another physcial
		 location, no new request should be allowed to the moving LPA. Otherwise, the system may become inconsistent.

		 GC starts on a physical block ------>  set barrier for physical block  ----(LPAs are read from flash)----->  set barrier for LPA  -----(LPA is written into its new location)------>  remove barrier for LPA
		
		 这些是用于垃圾回收和磨损均衡执行的系统状态一致性控制函数。
		 一旦 GC_and_WL_Unit_Base 开始将逻辑页面 (LPA) 从一个物理位置移动到另一个物理位置，
		 就不应允许对正在移动的 LPA 发出新的请求。否则，系统可能会变得不一致。
		 垃圾回收开始于一个物理块 ------> 为物理块设置屏障 ----(从闪存中读取 LPA)-----> 为 LPA 设置屏障 -----(LPA 被写入其新位置)------> 移除 LPA 的屏障。

		**********************************************************************************************************************/
		virtual void Set_barrier_for_accessing_physical_block(const NVM::FlashMemory::Physical_Page_Address& block_address) = 0;//在执行垃圾回收请求的最开始阶段，GC目标物理块（即选定的待擦除块）应该通过一个屏障进行保护。这个块内的逻辑页地址（LPA）在读取物理页面内容之前是未知的。因此，在垃圾回收执行开始时，为物理块设置了屏障。后来，当从物理块中读取逻辑页地址时，将使用上述功能来锁定每个逻辑页地址
		virtual void Set_barrier_for_accessing_lpa(const stream_id_type stream_id, const LPA_type lpa) = 0; //当GC单元（即GC_and_WL_Unit_Base）开始将LPA从一个物理页面移动到另一个物理页面时，它为访问LPA设置了一个障碍。这种障碍与CPU中的内存障碍非常相似，即在设置障碍之前发出的所有对lpa的访问仍然可以执行，但不允许新的访问。
		virtual void Set_barrier_for_accessing_mvpn(const stream_id_type stream_id, const MVPN_type mvpn) = 0; //当GC单元（即GC_and_WL_Unit_Base）开始将一个MVPN从一个物理页面移动到另一个物理页面时，它为访问MVPN设置了一个障碍。这种类型的障碍很像CPU中的内存障碍，即在设置障碍之前发出的所有对lpa的访问可以执行，但不允许任何新的访问。
		virtual void Remove_barrier_for_accessing_lpa(const stream_id_type stream_id, const LPA_type lpa) = 0; //消除了已设定的访问 LPA 的障碍（即，GC_and_WL_Unit_Base 单元成功将 LPA 从一个物理位置迁移到另一个物理位置）。
		virtual void Remove_barrier_for_accessing_mvpn(const stream_id_type stream_id, const MVPN_type mvpn) = 0; //消除了已设定的访问MVPN的障碍（即，GC_and_WL_Unit_Base单元成功完成了将MVPN从一个物理位置迁移到另一个物理位置）。
		virtual void Start_servicing_writes_for_overfull_plane(const NVM::FlashMemory::Physical_Page_Address plane_address) = 0;//当垃圾收集（GC）执行完成且该平面有足够数量的空闲页来处理写入时，将调用此功能。
	protected:
		FTL* ftl;
		NVM_PHY_ONFI* flash_controller;
		Flash_Block_Manager_Base* block_manager;//块管理器
		CMT_Sharing_Mode sharing_mode;//CMT的共享模式
		bool ideal_mapping_table;//如果映射是理想的，那么所有的映射条目都可以在DRAM中找到，不需要从闪存中读取映射条目。
		unsigned int no_of_input_streams;//stream 数量
		LHA_type max_logical_sector_address;//最大逻辑扇区地址

		unsigned int channel_count;//channel 数量
		unsigned int chip_no_per_channel;//每个channel的chip数量
		unsigned int die_no_per_chip;//每个chip的die数量
		unsigned int plane_no_per_die;//每个die的plane数量
		unsigned int block_no_per_plane;//每个plane的block数量
		unsigned int pages_no_per_block;//每个block的page数量
		unsigned int sector_no_per_page;//每个page的sector数量
		unsigned int page_size_in_byte;//page大小
		unsigned int total_physical_pages_no = 0;//总的物理页数
		unsigned int total_logical_pages_no = 0;//总的逻辑页数，排除冗余比例
		unsigned int page_no_per_channel = 0;//channel总page数
		unsigned int page_no_per_chip = 0;//chip总page数
		unsigned int page_no_per_die = 0;//die总page数
		unsigned int page_no_per_plane = 0;//plane总page数
		double overprovisioning_ratio;//冗余比
		bool fold_large_addresses;//是否折叠地址
		bool mapping_table_stored_on_flash;//映射表是否存储在flash中

		virtual bool query_cmt(NVM_Transaction_Flash* transaction) = 0;
		virtual PPA_type online_create_entry_for_reads(LPA_type lpa, const stream_id_type stream_id, NVM::FlashMemory::Physical_Page_Address& read_address, uint64_t read_sectors_bitmap) = 0;
		virtual void manage_user_transaction_facing_barrier(NVM_Transaction_Flash* transaction) = 0;
		virtual void manage_mapping_transaction_facing_barrier(stream_id_type stream_id, MVPN_type mvpn, bool read) = 0;
		virtual bool is_lpa_locked_for_gc(stream_id_type stream_id, LPA_type lpa) = 0;
		virtual bool is_mvpn_locked_for_gc(stream_id_type stream_id, MVPN_type mvpn) = 0;
	};
}

#endif // !ADDRESS_MAPPING_UNIT_BASE_H
