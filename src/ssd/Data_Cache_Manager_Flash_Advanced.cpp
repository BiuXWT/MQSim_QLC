#include <stdexcept>
#include "../nvm_chip/NVM_Types.h"
#include "Data_Cache_Manager_Flash_Advanced.h"
#include "NVM_Transaction_Flash_RD.h"
#include "NVM_Transaction_Flash_WR.h"
#include "FTL.h"

namespace SSD_Components
{
	Data_Cache_Manager_Flash_Advanced::Data_Cache_Manager_Flash_Advanced(const sim_object_id_type& id, Host_Interface_Base* host_interface, NVM_Firmware* firmware, NVM_PHY_ONFI* flash_controller,
		unsigned int total_capacity_in_bytes,
		unsigned int dram_row_size, unsigned int dram_data_rate, unsigned int dram_busrt_size, sim_time_type dram_tRCD, sim_time_type dram_tCL, sim_time_type dram_tRP,
		Caching_Mode* caching_mode_per_input_stream, Cache_Sharing_Mode sharing_mode,unsigned int stream_count,
		unsigned int sector_no_per_page, unsigned int back_pressure_buffer_max_depth)
		: Data_Cache_Manager_Base(id, host_interface, firmware, dram_row_size, dram_data_rate, dram_busrt_size, dram_tRCD, dram_tCL, dram_tRP, caching_mode_per_input_stream, sharing_mode, stream_count),
		flash_controller(flash_controller), capacity_in_bytes(total_capacity_in_bytes), sector_no_per_page(sector_no_per_page),	memory_channel_is_busy(false),
		dram_execution_list_turn(0), back_pressure_buffer_max_depth(back_pressure_buffer_max_depth)
	{
		capacity_in_pages = capacity_in_bytes / (SECTOR_SIZE_IN_BYTE * sector_no_per_page);
		switch (sharing_mode)
		{
			case SSD_Components::Cache_Sharing_Mode::SHARED:
			{
				Data_Cache_Flash* sharedCache = new Data_Cache_Flash(capacity_in_pages);
				per_stream_cache = new Data_Cache_Flash*[stream_count];
				for (unsigned int i = 0; i < stream_count; i++) {
					//每个stream共用sharedCache
					per_stream_cache[i] = sharedCache;
				}
				dram_execution_queue = new std::queue<Memory_Transfer_Info*>[1];
				waiting_user_requests_queue_for_dram_free_slot = new std::list<User_Request*>[1];
				this->back_pressure_buffer_depth = new unsigned int[1];
				this->back_pressure_buffer_depth[0] = 0;
				shared_dram_request_queue = true;
				break; 
			}
			case SSD_Components::Cache_Sharing_Mode::EQUAL_PARTITIONING:
				per_stream_cache = new Data_Cache_Flash*[stream_count];
				for (unsigned int i = 0; i < stream_count; i++) {
					per_stream_cache[i] = new Data_Cache_Flash(capacity_in_pages / stream_count);
				}
				dram_execution_queue = new std::queue<Memory_Transfer_Info*>[stream_count];
				waiting_user_requests_queue_for_dram_free_slot = new std::list<User_Request*>[stream_count];
				this->back_pressure_buffer_depth = new unsigned int[stream_count];
				for (unsigned int i = 0; i < stream_count; i++) {
					this->back_pressure_buffer_depth[i] = 0;
				}
				shared_dram_request_queue = false;
				break;
			default:
				break;
		}

		bloom_filter = new std::set<LPA_type>[stream_count];
	}
	
	Data_Cache_Manager_Flash_Advanced::~Data_Cache_Manager_Flash_Advanced()
	{
		switch (sharing_mode)
		{
			case SSD_Components::Cache_Sharing_Mode::SHARED:
			{
				delete per_stream_cache[0];
				while (dram_execution_queue[0].size()) {
					delete dram_execution_queue[0].front();
					dram_execution_queue[0].pop();
				}
				for (auto &req : waiting_user_requests_queue_for_dram_free_slot[0]) {
					delete req;
				}
				break;
			}
			case SSD_Components::Cache_Sharing_Mode::EQUAL_PARTITIONING:
				for (unsigned int i = 0; i < stream_count; i++) {
					delete per_stream_cache[i];
					while (dram_execution_queue[i].size()) {
						delete dram_execution_queue[i].front();
						dram_execution_queue[i].pop();
					}
					for (auto &req : waiting_user_requests_queue_for_dram_free_slot[i]) {
						delete req;
					}
				}
				break;
			default:
				break;
		}
		
		delete[] per_stream_cache;
		delete[] dram_execution_queue;
		delete[] waiting_user_requests_queue_for_dram_free_slot;
		delete[] bloom_filter;
	}

	void Data_Cache_Manager_Flash_Advanced::Setup_triggers()
	{
		Data_Cache_Manager_Base::Setup_triggers();
		flash_controller->ConnectToTransactionServicedSignal(handle_transaction_serviced_signal_from_PHY);
	}

	void Data_Cache_Manager_Flash_Advanced::Do_warmup(std::vector<Utils::Workload_Statistics*> workload_stats)
	{
		double total_write_arrival_rate = 0, total_read_arrival_rate = 0;
		switch (sharing_mode) {
			case Cache_Sharing_Mode::SHARED:
				//Estimate read arrival and write arrival rate
				//Estimate the queue length based on the arrival rate
				for (auto &stat : workload_stats) {
					switch (caching_mode_per_input_stream[stat->Stream_id]) {
						case Caching_Mode::TURNED_OFF:
							break;
						case Caching_Mode::READ_CACHE:
							if (stat->Type == Utils::Workload_Type::SYNTHETIC) {
							} else {
							}
							break;
						case Caching_Mode::WRITE_CACHE:
							if (stat->Type == Utils::Workload_Type::SYNTHETIC) {
								unsigned int total_pages_accessed = 1;
								switch (stat->Address_distribution_type)
								{
								case Utils::Address_Distribution_Type::STREAMING:
									break;
								case Utils::Address_Distribution_Type::RANDOM_HOTCOLD:
									break;
								case Utils::Address_Distribution_Type::RANDOM_UNIFORM:
									break;
								}
							} else {
							}
							break;
						case Caching_Mode::WRITE_READ_CACHE:
							//Put items on cache based on the accessed addresses
							if (stat->Type == Utils::Workload_Type::SYNTHETIC) {
							} else {
							}
							break;
					}
				}
				break;
			case Cache_Sharing_Mode::EQUAL_PARTITIONING:
				for (auto &stat : workload_stats) {
					switch (caching_mode_per_input_stream[stat->Stream_id])
					{
						case Caching_Mode::TURNED_OFF:
							break;
						case Caching_Mode::READ_CACHE:
							//Put items on cache based on the accessed addresses
							if (stat->Type == Utils::Workload_Type::SYNTHETIC) {
							} else {
							}
							break;
						case Caching_Mode::WRITE_CACHE:
							//Estimate the request arrival rate
							//Estimate the request service rate
							//Estimate the average size of requests in the cache
							//Fillup the cache space based on accessed adddresses to the estimated average cache size
							if (stat->Type == Utils::Workload_Type::SYNTHETIC) {
								//Estimate average write service rate
								unsigned int total_pages_accessed = 1;
								/*double average_write_arrival_rate, stdev_write_arrival_rate;
								double average_read_arrival_rate, stdev_read_arrival_rate;
								double average_write_service_time, average_read_service_time;*/
								switch (stat->Address_distribution_type)
								{
									case Utils::Address_Distribution_Type::STREAMING:
										break;
									case Utils::Address_Distribution_Type::RANDOM_HOTCOLD:
										break;
									case Utils::Address_Distribution_Type::RANDOM_UNIFORM:
										break;
								}
							} else {
							}
							break;
						case Caching_Mode::WRITE_READ_CACHE:
							//Put items on cache based on the accessed addresses
							if (stat->Type == Utils::Workload_Type::SYNTHETIC) {
							} else {
							}
							break;
					}
				}
				break;
		}
	}
	std::string to_string(Transaction_Type type) {
		switch (type) {
		case Transaction_Type::READ:    return "READ";
		case Transaction_Type::WRITE:   return "WRITE";
		case Transaction_Type::ERASE:   return "ERASE";
		case Transaction_Type::UNKOWN: return "UNKOWN";
		default:                        return "INVALID";
		}
	}
	std::string to_string(Transaction_Source_Type type) {
		switch (type) {
		case Transaction_Source_Type::USERIO: return "USERIO";
		case Transaction_Source_Type::CACHE: return "CACHE";
		case Transaction_Source_Type::GC_WL: return "GC_WL";
		case Transaction_Source_Type::MAPPING: return "MAPPING";
		default:                        return "INVALID";
		}
	}
	void Data_Cache_Manager_Flash_Advanced::process_new_user_request(User_Request* user_request)
	{
		//This condition shouldn't happen, but we check it
		if (user_request->Transaction_list.size() == 0) {
			return;
		}
		//auto sqe = dynamic_cast<Submission_Queue_Entry*>(user_request->IO_command_info);
		// DEBUG_BIU((user_request->Type == UserRequestType::READ ? "READ" : "WRITE") << "#LBA:" << user_request->Start_LBA 
		// 	<< "#size:" << user_request->Size_in_byte << "bytes==" << user_request->SizeInSectors << "sectors" 
		// 	<< "#data:" << user_request->Data
		// 	<<"#stream_id:"<<user_request->Stream_id<<"#id:"<<user_request->ID);
		
		for (auto it = user_request->Transaction_list.begin(); it != user_request->Transaction_list.end(); it++) {
			auto tr = (*it);
			// DEBUG_BIU("tr_type="<<to_string(tr->Type)<<";tr_src_type="<<to_string(tr->Source))
		}

		if (user_request->Type == UserRequestType::READ) {
			switch (caching_mode_per_input_stream[user_request->Stream_id]) {
				case Caching_Mode::TURNED_OFF:
					static_cast<FTL*>(nvm_firmware)->Address_Mapping_Unit->Translate_lpa_to_ppa_and_dispatch(user_request->Transaction_list);
					return;
				case Caching_Mode::WRITE_CACHE:
				case Caching_Mode::READ_CACHE:
				case Caching_Mode::WRITE_READ_CACHE:
				{
					std::list<NVM_Transaction*>::iterator it = user_request->Transaction_list.begin();
					while (it != user_request->Transaction_list.end()) {
						NVM_Transaction_Flash_RD* tr = (NVM_Transaction_Flash_RD*)(*it);
						if (per_stream_cache[tr->Stream_id]->Exists(tr->Stream_id, tr->LPA)) {
							//缓存命中
							page_status_type available_sectors_bitmap = per_stream_cache[tr->Stream_id]->Get_slot(tr->Stream_id, tr->LPA).State_bitmap_of_existing_sectors & tr->read_sectors_bitmap;
							if (available_sectors_bitmap == tr->read_sectors_bitmap) {
								//全部sector有效   sector 512字节
								user_request->Sectors_serviced_from_cache += count_sector_no_from_status_bitmap(tr->read_sectors_bitmap);
								user_request->Transaction_list.erase(it++);//the ++ operation should happen here, otherwise the iterator will be part of the list after erasing it from the list
							} else if (available_sectors_bitmap != 0) {
								//缓存中部分sector数据有效
								user_request->Sectors_serviced_from_cache += count_sector_no_from_status_bitmap(available_sectors_bitmap);
								tr->read_sectors_bitmap = (tr->read_sectors_bitmap & ~available_sectors_bitmap);
								tr->Data_and_metadata_size_in_byte -= count_sector_no_from_status_bitmap(available_sectors_bitmap) * SECTOR_SIZE_IN_BYTE;
								it++;
							} else {
								//缓存中无有效数据
								it++;
							}
						} else {
							//缓存未命中
							it++;
						}
					}

					if (user_request->Sectors_serviced_from_cache > 0) {
						//缓存命中数据，读dram缓存
						Memory_Transfer_Info* transfer_info = new Memory_Transfer_Info;
						transfer_info->Size_in_bytes = user_request->Sectors_serviced_from_cache * SECTOR_SIZE_IN_BYTE;
						transfer_info->Related_request = user_request;
						transfer_info->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_USERIO_FINISHED;
						transfer_info->Stream_id = user_request->Stream_id;
						service_dram_access_request(transfer_info);
					}
					if (user_request->Transaction_list.size() > 0) {
						static_cast<FTL*>(nvm_firmware)->Address_Mapping_Unit->Translate_lpa_to_ppa_and_dispatch(user_request->Transaction_list);
					}

					return;
				}
			}
		}
		else//This is a write request
		{
			switch (caching_mode_per_input_stream[user_request->Stream_id])
			{
				case Caching_Mode::TURNED_OFF:
				case Caching_Mode::READ_CACHE:
					static_cast<FTL*>(nvm_firmware)->Address_Mapping_Unit->Translate_lpa_to_ppa_and_dispatch(user_request->Transaction_list);
					return;
				case Caching_Mode::WRITE_CACHE://The data cache manger unit performs like a destage buffer
				case Caching_Mode::WRITE_READ_CACHE:
				{
					write_to_destage_buffer(user_request);

					int queue_id = user_request->Stream_id;
					if (shared_dram_request_queue) {
						queue_id = 0;
					}
					if (user_request->Transaction_list.size() > 0) {
						waiting_user_requests_queue_for_dram_free_slot[queue_id].push_back(user_request);
					}
					return;
				}
			}
		}
	}

	void Data_Cache_Manager_Flash_Advanced::write_to_destage_buffer(User_Request* user_request)
	{
		//To eliminate race condition, MQSim assumes the management information and user data are stored in separate DRAM modules
		unsigned int cache_eviction_read_size_in_sectors = 0;//The size of data evicted from cache
		unsigned int flash_written_back_write_size_in_sectors = 0;//The size of data that is both written back to flash and written to DRAM
		unsigned int dram_write_size_in_sectors = 0;//The size of data written to DRAM (must be >= flash_written_back_write_size_in_sectors)
		std::list<NVM_Transaction*>* evicted_cache_slots = new std::list<NVM_Transaction*>;
		std::list<NVM_Transaction*> writeback_transactions;
		auto it = user_request->Transaction_list.begin();

		int queue_id = user_request->Stream_id;
		if (shared_dram_request_queue) {
			queue_id = 0;
		}

		while (it != user_request->Transaction_list.end() 
			&& (back_pressure_buffer_depth[queue_id] + cache_eviction_read_size_in_sectors + flash_written_back_write_size_in_sectors) < back_pressure_buffer_max_depth) {
			NVM_Transaction_Flash_WR* tr = (NVM_Transaction_Flash_WR*)(*it);
			//If the logical address already exists in the cache
			if (per_stream_cache[tr->Stream_id]->Exists(tr->Stream_id, tr->LPA)) {
				/*MQSim should get rid of writting stale data to the cache.
				* This situation may result from out-of-order transaction execution
				MQSim 应该避免将过时的数据写入缓存。这个情况可能是由于事务执行顺序不正确导致的。*/
				Data_Cache_Slot_Type slot = per_stream_cache[tr->Stream_id]->Get_slot(tr->Stream_id, tr->LPA);
				sim_time_type timestamp = slot.Timestamp;
				NVM::memory_content_type content = slot.Content;
				if (tr->DataTimeStamp > timestamp) {
					timestamp = tr->DataTimeStamp;
					content = tr->Content;
				}
				per_stream_cache[tr->Stream_id]->Update_data(tr->Stream_id, tr->LPA, content, timestamp, tr->write_sectors_bitmap | slot.State_bitmap_of_existing_sectors);
			} else {//the logical address is not in the cache
				if (!per_stream_cache[tr->Stream_id]->Check_free_slot_availability()) {
					Data_Cache_Slot_Type evicted_slot = per_stream_cache[tr->Stream_id]->Evict_one_slot_lru();//回收一个缓存块
					if (evicted_slot.Status == Cache_Slot_Status::DIRTY_NO_FLASH_WRITEBACK) {
						evicted_cache_slots->push_back(new NVM_Transaction_Flash_WR(Transaction_Source_Type::CACHE,
							tr->Stream_id, count_sector_no_from_status_bitmap(evicted_slot.State_bitmap_of_existing_sectors) * SECTOR_SIZE_IN_BYTE,
							evicted_slot.LPA, NULL, IO_Flow_Priority_Class::URGENT, evicted_slot.Content, evicted_slot.State_bitmap_of_existing_sectors, evicted_slot.Timestamp));
						cache_eviction_read_size_in_sectors += count_sector_no_from_status_bitmap(evicted_slot.State_bitmap_of_existing_sectors);
						//DEBUG2("Evicting page" << evicted_slot.LPA << " from write buffer ")
					}
				}
				per_stream_cache[tr->Stream_id]->Insert_write_data(tr->Stream_id, tr->LPA, tr->Content, tr->DataTimeStamp, tr->write_sectors_bitmap);
			}
			dram_write_size_in_sectors += count_sector_no_from_status_bitmap(tr->write_sectors_bitmap);
			//hot/cold data separation
			if (bloom_filter[tr->Stream_id].find(tr->LPA) == bloom_filter[tr->Stream_id].end())//数据不在filter中（冷数据）
			{
				per_stream_cache[tr->Stream_id]->Change_slot_status_to_writeback(tr->Stream_id, tr->LPA); //Eagerly write back cold data，标记冷数据写回
				flash_written_back_write_size_in_sectors += count_sector_no_from_status_bitmap(tr->write_sectors_bitmap);
				bloom_filter[user_request->Stream_id].insert(tr->LPA);///将数据加入filter中,标记为热数据
				writeback_transactions.push_back(tr);
			}
			user_request->Transaction_list.erase(it++);
		}
		
		user_request->Sectors_serviced_from_cache += dram_write_size_in_sectors;//This is very important update. It is used to decide when all data sectors of a user request are serviced
		back_pressure_buffer_depth[queue_id] += cache_eviction_read_size_in_sectors + flash_written_back_write_size_in_sectors;

		//Issue memory read for cache evictions//如果存在被驱逐的缓存项（evicted_cache_slots非空），则发起一次DRAM读操作，用于将这些被驱逐的数据从DRAM中读回缓存。
		if (evicted_cache_slots->size() > 0) {
			Memory_Transfer_Info* read_transfer_info = new Memory_Transfer_Info;
			read_transfer_info->Size_in_bytes = cache_eviction_read_size_in_sectors * SECTOR_SIZE_IN_BYTE;
			read_transfer_info->Related_request = evicted_cache_slots;
			read_transfer_info->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_CACHE_EVICTION_FINISHED;
			read_transfer_info->Stream_id = user_request->Stream_id;
			service_dram_access_request(read_transfer_info);
		}

		//Issue memory write to write data to DRAM//如果有需要写入的数据（dram_write_size_in_sectors > 0），则发起一次DRAM写操作，将用户数据写入内存
		if (dram_write_size_in_sectors) {
			Memory_Transfer_Info* write_transfer_info = new Memory_Transfer_Info;
			write_transfer_info->Size_in_bytes = dram_write_size_in_sectors * SECTOR_SIZE_IN_BYTE;
			write_transfer_info->Related_request = user_request;
			write_transfer_info->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_WRITE_FOR_USERIO_FINISHED;
			write_transfer_info->Stream_id = user_request->Stream_id;
			service_dram_access_request(write_transfer_info);
		}

		//If any writeback should be performed, then issue flash write transactions
		if (writeback_transactions.size() > 0) {
					static_cast<FTL*>(nvm_firmware)->Address_Mapping_Unit->Translate_lpa_to_ppa_and_dispatch(writeback_transactions);
		}
		
		//Reset control data structures used for hot/cold separation         //重置用于冷热分离的控制数据结构 
		if (Simulator->Time() > next_bloom_filter_reset_milestone) {
			bloom_filter[user_request->Stream_id].clear();
			next_bloom_filter_reset_milestone = Simulator->Time() + bloom_filter_reset_step;
		}
	}

	void Data_Cache_Manager_Flash_Advanced::handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* transaction)
	{
		/**
		 * back_pressure_buffer_depth：用于控制缓存写入压力，防止后端 Flash 负载过高。
			bloom_filter：用于冷热数据分离，热数据优先写入缓存。
			write_to_destage_buffer：实际写入缓存逻辑，包括缓存插入、驱逐、更新等。
		 */
		//First check if the transaction source is a user request or the cache itself
        //仅处理来自用户 I/O 或缓存本身的事务，其他事件直接return
		if (transaction->Source != Transaction_Source_Type::USERIO && transaction->Source != Transaction_Source_Type::CACHE) {
			return;
		}

		if (transaction->Source == Transaction_Source_Type::USERIO) {
			_my_instance->broadcast_user_memory_transaction_serviced_signal(transaction);
		}

		/* This is an update read (a read that is generated for a write request that partially updates page data).
		*  An update read transaction is issued in Address Mapping Unit, but is consumed in data cache manager.
		这是一个更新读取（为部分更新页面数据的写请求生成的读取）。更新读取事务在地址映射单元中发出，但在数据缓存管理器中使用。*/
		if (transaction->Type == Transaction_Type::READ) {//处理读事务
			if (((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite != NULL) {//处理为部分写操作生成的更新读取，完成后解除关联
				((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
				return;
			}

			switch (Data_Cache_Manager_Flash_Advanced::caching_mode_per_input_stream[transaction->Stream_id])
			{
				case Caching_Mode::TURNED_OFF:
				case Caching_Mode::WRITE_CACHE:
				//情况一：缓存关闭或仅写缓存:直接移除事务
					transaction->UserIORequest->Transaction_list.remove(transaction);
					if (_my_instance->is_user_request_finished(transaction->UserIORequest)) {
						_my_instance->broadcast_user_request_serviced_signal(transaction->UserIORequest);
					}
					break;
				case Caching_Mode::READ_CACHE:
				case Caching_Mode::WRITE_READ_CACHE:
				//情况二：启用读缓存或读写缓存:
				{					
					if (((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Exists(transaction->Stream_id, transaction->LPA)) {//缓存命中（命中 LPA）
						/*MQSim should get rid of writting stale data to the cache.
						* This situation may result from out-of-order transaction execution
						MQSim 应该避免将过时的数据写入缓存。这个情况可能是由于事务执行顺序不正确导致的。*/
						Data_Cache_Slot_Type slot = ((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Get_slot(transaction->Stream_id, transaction->LPA);
						sim_time_type timestamp = slot.Timestamp;
						NVM::memory_content_type content = slot.Content;
						if (((NVM_Transaction_Flash_RD*)transaction)->DataTimeStamp > timestamp) {
							timestamp = ((NVM_Transaction_Flash_RD*)transaction)->DataTimeStamp;
							content = ((NVM_Transaction_Flash_RD*)transaction)->Content;
						}

						((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Update_data(transaction->Stream_id, transaction->LPA, content,
							timestamp, ((NVM_Transaction_Flash_RD*)transaction)->read_sectors_bitmap | slot.State_bitmap_of_existing_sectors);//更新缓存
					} else  {
						if (!((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Check_free_slot_availability()) {
							// 驱逐一个缓存项（LRU）
							std::list<NVM_Transaction*>* evicted_cache_slots = new std::list<NVM_Transaction*>;
							Data_Cache_Slot_Type evicted_slot = ((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Evict_one_slot_lru();
							if (evicted_slot.Status == Cache_Slot_Status::DIRTY_NO_FLASH_WRITEBACK) {
								Memory_Transfer_Info* transfer_info = new Memory_Transfer_Info;
								transfer_info->Size_in_bytes = count_sector_no_from_status_bitmap(evicted_slot.State_bitmap_of_existing_sectors) * SECTOR_SIZE_IN_BYTE;
								evicted_cache_slots->push_back(new NVM_Transaction_Flash_WR(Transaction_Source_Type::USERIO,
									transaction->Stream_id, transfer_info->Size_in_bytes, evicted_slot.LPA, NULL, IO_Flow_Priority_Class::UNDEFINED, evicted_slot.Content,
									evicted_slot.State_bitmap_of_existing_sectors, evicted_slot.Timestamp));
								transfer_info->Related_request = evicted_cache_slots;
								transfer_info->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_CACHE_EVICTION_FINISHED;
								transfer_info->Stream_id = transaction->Stream_id;
								unsigned int cache_eviction_read_size_in_sectors = count_sector_no_from_status_bitmap(evicted_slot.State_bitmap_of_existing_sectors);
								int sharing_id = transaction->Stream_id;
								if (((Data_Cache_Manager_Flash_Advanced*)_my_instance)->shared_dram_request_queue) {
									sharing_id = 0;
								}
								((Data_Cache_Manager_Flash_Advanced*)_my_instance)->back_pressure_buffer_depth[sharing_id] += cache_eviction_read_size_in_sectors;
								((Data_Cache_Manager_Flash_Advanced*)_my_instance)->service_dram_access_request(transfer_info);
							}
						}
						//// 插入新数据到缓存
						((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Insert_read_data(transaction->Stream_id, transaction->LPA,
							((NVM_Transaction_Flash_RD*)transaction)->Content, ((NVM_Transaction_Flash_RD*)transaction)->DataTimeStamp, ((NVM_Transaction_Flash_RD*)transaction)->read_sectors_bitmap);

						Memory_Transfer_Info* transfer_info = new Memory_Transfer_Info;
						transfer_info->Size_in_bytes = count_sector_no_from_status_bitmap(((NVM_Transaction_Flash_RD*)transaction)->read_sectors_bitmap) * SECTOR_SIZE_IN_BYTE;
						transfer_info->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_WRITE_FOR_CACHE_FINISHED;
						transfer_info->Stream_id = transaction->Stream_id;
						((Data_Cache_Manager_Flash_Advanced*)_my_instance)->service_dram_access_request(transfer_info);
					}

					//完成USERIO
					transaction->UserIORequest->Transaction_list.remove(transaction);
					if (_my_instance->is_user_request_finished(transaction->UserIORequest)) {
						_my_instance->broadcast_user_request_serviced_signal(transaction->UserIORequest);
					}
					break;
				}
			}
		} else {//This is a write request
			switch (Data_Cache_Manager_Flash_Advanced::caching_mode_per_input_stream[transaction->Stream_id])
			{
				case Caching_Mode::TURNED_OFF:
				case Caching_Mode::READ_CACHE:
				//情况一：缓存关闭或仅读缓存:直接移除事务
					transaction->UserIORequest->Transaction_list.remove(transaction);
					if (_my_instance->is_user_request_finished(transaction->UserIORequest)) {
						_my_instance->broadcast_user_request_serviced_signal(transaction->UserIORequest);
					}
					break;
				case Caching_Mode::WRITE_CACHE:
				case Caching_Mode::WRITE_READ_CACHE:
				{
					int sharing_id = transaction->Stream_id;
					if (((Data_Cache_Manager_Flash_Advanced*)_my_instance)->shared_dram_request_queue) {
						sharing_id = 0;
					}
					//背压控制
					((Data_Cache_Manager_Flash_Advanced*)_my_instance)->back_pressure_buffer_depth[sharing_id] -= transaction->Data_and_metadata_size_in_byte / SECTOR_SIZE_IN_BYTE + (transaction->Data_and_metadata_size_in_byte % SECTOR_SIZE_IN_BYTE == 0 ? 0 : 1);

					//存在对应缓存项 
					if (((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Exists(transaction->Stream_id, ((NVM_Transaction_Flash_WR*)transaction)->LPA)) {
						//获取缓存项
						Data_Cache_Slot_Type slot = ((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Get_slot(transaction->Stream_id, ((NVM_Transaction_Flash_WR*)transaction)->LPA);
						sim_time_type timestamp = slot.Timestamp;
						NVM::memory_content_type content = slot.Content;
						if (((NVM_Transaction_Flash_WR*)transaction)->DataTimeStamp >= timestamp) {//数据时间戳 >= 缓存时间戳
							//移出缓存
							((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Remove_slot(transaction->Stream_id, ((NVM_Transaction_Flash_WR*)transaction)->LPA);
						}
					}
					
					//处理等待队列中的用户请求
					auto user_request = ((Data_Cache_Manager_Flash_Advanced*)_my_instance)->waiting_user_requests_queue_for_dram_free_slot[sharing_id].begin();
					while (user_request != ((Data_Cache_Manager_Flash_Advanced*)_my_instance)->waiting_user_requests_queue_for_dram_free_slot[sharing_id].end())
					{
						((Data_Cache_Manager_Flash_Advanced*)_my_instance)->write_to_destage_buffer(*user_request);//写入缓存
						if ((*user_request)->Transaction_list.size() == 0) {
							//     如果事务列表为空，从等待队列中移除
							((Data_Cache_Manager_Flash_Advanced*)_my_instance)->waiting_user_requests_queue_for_dram_free_slot[sharing_id].erase(user_request++);
						} else {
							user_request++;
						}
						//The traffic load on the backend is high and the waiting requests cannot be serviced
						if (((Data_Cache_Manager_Flash_Advanced*)_my_instance)->back_pressure_buffer_depth[sharing_id] > ((Data_Cache_Manager_Flash_Advanced*)_my_instance)->back_pressure_buffer_max_depth) {
							//     如果 back_pressure_buffer_depth 超限，停止处理
							break;
						}
					}

					/*
					//缓存压力低时主动写回脏数据
					if (_my_instance->back_pressure_buffer_depth[sharing_id] < _my_instance->back_pressure_buffer_max_depth)//The traffic load on the backend is low and the waiting requests can be serviced
					{
						std::list<NVM_Transaction*>* evicted_cache_slots = new std::list<NVM_Transaction*>;
						while (!((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Empty())
						{
							DataCacheSlotType evicted_slot = ((Data_Cache_Manager_Flash_Advanced*)_my_instance)->per_stream_cache[transaction->Stream_id]->Evict_one_dirty_slot();
							if (evicted_slot.Status != CacheSlotStatus::EMPTY)
							{
								evicted_cache_slots->push_back(new NVM_Transaction_Flash_WR(Transaction_Source_Type::CACHE,
									transaction->Stream_id, count_sector_no_from_status_bitmap(evicted_slot.State_bitmap_of_existing_sectors) * SECTOR_SIZE_IN_BYTE,
									evicted_slot.LPA, NULL, IO_Flow_Priority_Class::UNDEFINED, evicted_slot.Content, evicted_slot.State_bitmap_of_existing_sectors, evicted_slot.Timestamp));
								_my_instance->back_pressure_buffer_depth[sharing_id] += count_sector_no_from_status_bitmap(evicted_slot.State_bitmap_of_existing_sectors);
								cache_eviction_read_size_in_sectors += count_sector_no_from_status_bitmap(evicted_slot.State_bitmap_of_existing_sectors);
							}
							else break;
							if (_my_instance->back_pressure_buffer_depth[sharing_id] >= _my_instance->back_pressure_buffer_max_depth)
								break;
						}
						
						if (cache_eviction_read_size_in_sectors > 0)
						{
							Memory_Transfer_Info* read_transfer_info = new Memory_Transfer_Info;
							read_transfer_info->Size_in_bytes = cache_eviction_read_size_in_sectors * SECTOR_SIZE_IN_BYTE;
							read_transfer_info->Related_request = evicted_cache_slots;
							read_transfer_info->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_CACHE_EVICTION_FINISHED;
							read_transfer_info->Stream_id = transaction->Stream_id;
							((Data_Cache_Manager_Flash_Advanced*)_my_instance)->service_dram_access_request(read_transfer_info);
						}
					}*/

					break;
				}
			}
		}
	}

	void Data_Cache_Manager_Flash_Advanced::service_dram_access_request(Memory_Transfer_Info* request_info)
	{
		if (memory_channel_is_busy) {
			if(shared_dram_request_queue) {
				dram_execution_queue[0].push(request_info);
			} else {
				dram_execution_queue[request_info->Stream_id].push(request_info);
			}
		} else {
			Simulator->Register_sim_event(Simulator->Time() + estimate_dram_access_time(request_info->Size_in_bytes, dram_row_size,
				dram_busrt_size, dram_burst_transfer_time_ddr, dram_tRCD, dram_tCL, dram_tRP),
				this, request_info, static_cast<int>(request_info->next_event_type));
			memory_channel_is_busy = true;
			dram_execution_list_turn = request_info->Stream_id;
		}
	}

	void Data_Cache_Manager_Flash_Advanced::Execute_simulator_event(MQSimEngine::Sim_Event* ev)
	{//从dram中读取缓存数据
		Data_Cache_Simulation_Event_Type eventType = (Data_Cache_Simulation_Event_Type)ev->Type;
		Memory_Transfer_Info* transfer_info = (Memory_Transfer_Info*)ev->Parameters;

		switch (eventType)
		{
			case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_USERIO_FINISHED://A user read is service from DRAM cache
			case Data_Cache_Simulation_Event_Type::MEMORY_WRITE_FOR_USERIO_FINISHED:
				((User_Request*)(transfer_info)->Related_request)->Sectors_serviced_from_cache -= transfer_info->Size_in_bytes / SECTOR_SIZE_IN_BYTE;
				if (is_user_request_finished((User_Request*)(transfer_info)->Related_request))
					broadcast_user_request_serviced_signal(((User_Request*)(transfer_info)->Related_request));
				break;
			case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_CACHE_EVICTION_FINISHED://Reading data from DRAM and writing it back to the flash storage
				static_cast<FTL*>(nvm_firmware)->Address_Mapping_Unit->Translate_lpa_to_ppa_and_dispatch(*((std::list<NVM_Transaction*>*)(transfer_info->Related_request)));
				delete (std::list<NVM_Transaction*>*)transfer_info->Related_request;
				break;
			case Data_Cache_Simulation_Event_Type::MEMORY_WRITE_FOR_CACHE_FINISHED://The recently read data from flash is written back to memory to support future user read requests
				break;
		}
		delete transfer_info;

		memory_channel_is_busy = false;
		if (shared_dram_request_queue)	{
			if (dram_execution_queue[0].size() > 0)	{
				Memory_Transfer_Info* transfer_info = dram_execution_queue[0].front();
				dram_execution_queue[0].pop();
				Simulator->Register_sim_event(Simulator->Time() + estimate_dram_access_time(transfer_info->Size_in_bytes, dram_row_size, dram_busrt_size,
					dram_burst_transfer_time_ddr, dram_tRCD, dram_tCL, dram_tRP),
					this, transfer_info, static_cast<int>(transfer_info->next_event_type));
				memory_channel_is_busy = true;
			}
		} else {
			for (unsigned int i = 0; i < stream_count; i++) {
				dram_execution_list_turn++;
				dram_execution_list_turn %= stream_count;
				if (dram_execution_queue[dram_execution_list_turn].size() > 0) {
					Memory_Transfer_Info* transfer_info = dram_execution_queue[dram_execution_list_turn].front();
					dram_execution_queue[dram_execution_list_turn].pop();
					Simulator->Register_sim_event(Simulator->Time() + estimate_dram_access_time(transfer_info->Size_in_bytes, dram_row_size, dram_busrt_size,
						dram_burst_transfer_time_ddr, dram_tRCD, dram_tCL, dram_tRP),
						this, transfer_info, static_cast<int>(transfer_info->next_event_type));
					memory_channel_is_busy = true;
					break;
				}
			}
		}
	}
}
