#define NOMINMAX  // 禁用Windows头文件中的min和max宏定义，避免与标准库的std::min和std::max冲突
#include <iostream>     // 输入输出流
#include <vector>       // 向量容器
#include <algorithm>    // 算法库 (用于排序、最大最小值等)
#include <iomanip>      // IO 流格式控制 (如设置小数位数)
#include <windows.h>    // Windows API
#include <commdlg.h>    // 通用对话框 (文件选择对话框)
#include <string>       // 字符串类

class MidiDifficultyCalculator {  // 定义一个MIDI难度计算器类

	// 音符结构体
	struct Note {
		int noteNumber;     // MIDI音符编号 (0~127)，表示音高
		double startTime;   // 开始时间 (毫秒)
		double endTime;     // 结束时间 (毫秒)
	};

	// 音结构体 (可以是单音或和弦)
	struct Sound {
		std::vector<Note> notes;  // 音包含多个音符
		double startTime;         // 音开始时间

		// 判断是否为单音 (单个音符)
		bool isSingleNote() const {
			return notes.size() == 1;
		}

		// 判断是否为和弦 (多个音符)
		bool isChord() const {
			return notes.size() > 1;
		}
	};

	// 音轨数据结构
	struct TrackData {
		std::vector<std::vector<Sound>> channels;  // 16个通道的音数据
		std::vector<int> channelInstruments;       // 16个通道的当前乐器
		unsigned int trackIndex;                   // 音轨索引
		
		TrackData(unsigned int index) : trackIndex(index) {
			channels.resize(16);
			channelInstruments.assign(16, 0); // 默认乐器都是钢琴
		}
		
		// 判断通道是否有效
		bool isChannelValid(int channel) const {
			if (channel < 0 || channel >= (int)channels.size()) return false;
			int soundCount = channels[channel].size();
			int noteCount = 0;  // 音符数
			for (const auto& sound : channels[channel]) {
				noteCount += sound.notes.size();
			}
			return (soundCount > 0 || noteCount > 0);
		}
		
		// 获取通道的单音数、和弦数和音符数
		void getChannelStats(int channel, int& chordCount, int& singleNoteCount, int& noteCount) const {
			singleNoteCount = 0;  // 初始化单音数
			chordCount = 0;       // 初始化和弦数
			noteCount = 0;        // 初始化音符数
			for (const auto& sound : channels[channel]) {
				if (sound.isSingleNote()) {singleNoteCount++;}
				else if (sound.isChord()) {chordCount++;}
				noteCount += sound.notes.size();
			}
		}
	};

private:
	int bpm;  // 每分钟节拍数 (速度)
	std::vector<std::vector<Sound> > channels;  // 16个通道的音数据 (用于兼容)
	std::vector<TrackData> allTracks;           // 所有轨道的数据
	std::vector<int> finalChannelInstruments;   // 最终每个通道的乐器 (基于所有轨道)

public:
	// 构造函数
	MidiDifficultyCalculator() : bpm(120) {
		channels.resize(16);
		finalChannelInstruments.assign(16, 0);
	}

	// 打开文件选择对话框
	std::string openFileSelector() {
		OPENFILENAMEA ofn;  // 使用ANSI版本的Windows文件对话框结构体
		char szFile[260] = { 0 };  // 存储文件路径的缓冲区 (使用char类型字符串, 用于适配ANSI版本的API)

		ZeroMemory(&ofn, sizeof(ofn));  // 初始化结构体
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = szFile;  // 指定文件路径缓冲区
		ofn.nMaxFile = sizeof(szFile);  // ANSI版本, 使用sizeof
		ofn.lpstrFilter = "MIDI文件\0*.mid;*.midi\0所有文件\0*.*\0";  // 文件类型过滤器
		ofn.nFilterIndex = 1;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;  // 对话框标志

		if (GetOpenFileNameA(&ofn)) {  // 使用ANSI版本的API
			return std::string(szFile);  // 直接返回选中的文件路径
		}
		return "";  // 如果没有选择文件, 则返回空字符串
	}

	// 辅助函数: 解析可变长度值
	unsigned int readVariableLength(std::vector<unsigned char>::const_iterator& it) {
		unsigned int value = 0;
		unsigned char byte;
		int count = 0; // 防止无限循环

		do {
			if (count >= 4) { // 可变长度值最多4个字节
				break;
			}
			byte = *it++;  // 读取一个字节并移动迭代器
			value = (value << 7) | (byte & 0x7F);  // 拼接可变长度值
			count++;
		} while ((byte & 0x80) && count < 4);  // MSB为1表示还有后续字节

		return value;
	}

	// 计算时间 (毫秒)
	double calculateTimeInMs(unsigned int ticks, int division) {
		// 处理负数division (SMPTE格式)
		if (division < 0) {
			// 负数表示SMPTE格式: -frames_per_second * ticks_per_frame
			int smpte_fps = -(division >> 8);
			int ticks_per_frame = division & 0xFF;

			// 简化处理, 假设30fps
			if (smpte_fps != 0 && ticks_per_frame != 0) {
				double seconds = static_cast<double>(ticks) / (abs(smpte_fps) * ticks_per_frame);
				return seconds * 1000.0;
			}
			return 0.0;
		}
		else {
			// 正数表示ticks per quarter note
			if (division == 0 || bpm == 0) return 0.0;

			// 时间 (秒) = (ticks / division) * (60.0 / bpm)
			// 时间 (毫秒) = 时间 (秒) * 1000
			double quarters = static_cast<double>(ticks) / division;
			double seconds = quarters * (60.0 / bpm);
			return seconds * 1000.0;
		}
	}

	// 解析轨道事件
	void parseTrackEvents(const std::vector<unsigned char>& trackData, int division) {
		auto it = trackData.begin();
		auto end = trackData.end();

		unsigned int currentTime = 0;
		unsigned char lastStatus = 0;

		while (it != end) {
			// 检查是否有足够的数据读取delta time
			if (it == end) break;

			// 读取delta time
			unsigned int deltaTime = readVariableLength(it);
			currentTime += deltaTime;

			// 检查是否有足够的数据读取状态字节
			if (it == end) break;

			// 读取状态字节
			unsigned char status = *it++;
			if ((status & 0x80) == 0) {
				// 运行状态 - 使用上一个状态
				if (it != trackData.begin()) {
					it--; // 回退一位
					status = lastStatus;
				}
				else {
					break;
				}
			}
			else {lastStatus = status;}

			unsigned char eventType = status & 0xF0;  // 高4位是事件类型
			int currentChannel = status & 0x0F;       // 低4位是通道号

			switch (eventType) {
			case 0x80: // Note Off
			case 0x90: { // Note On
				// 检查是否有足够的数据读取note和velocity
				if (std::distance(it, end) < 2) break;

				unsigned char note = *it++;
				unsigned char velocity = *it++;

				// 处理音符开启/关闭事件, Note On力度为0等同于Note Off
				if (eventType == 0x90 && velocity == 0) {}  // 简化处理，当作Note Off
				
				else if (eventType == 0x90) {
					// 计算时间 (毫秒)
					double timeInMs = calculateTimeInMs(currentTime, division);

					// 创建新音符
					Note newNote;
					newNote.noteNumber = note;
					newNote.startTime = timeInMs;
					newNote.endTime = timeInMs + 500; // 默认持续时间

					// 添加到对应通道
					if (currentChannel < (int)channels.size()) {
						// 将同时刻的音符记为同一个音处理
						if (channels[currentChannel].empty() ||
							channels[currentChannel].back().startTime != timeInMs) {
							Sound newSound;
							newSound.startTime = timeInMs;
							newSound.notes.push_back(newNote);
							channels[currentChannel].push_back(newSound);
						}
						else {channels[currentChannel].back().notes.push_back(newNote);}
					}
				}
				break;
			}
			case 0xA0: // Polyphonic Key Pressure (多键触后)
			case 0xB0: // Control Change (控制改变)
			case 0xC0: // Program Change (程序改变)
			case 0xD0: // Channel Pressure (通道压力)
			case 0xE0: { // Pitch Bend (弯音)
				// 这些事件的数据字节数是固定的
				int dataBytes = (eventType == 0xC0 || eventType == 0xD0) ? 1 : 2;
				if (std::distance(it, end) < dataBytes) break;
				for (int i = 0; i < dataBytes; ++i) {++it;}
				break;
			}
			case 0xF0: { // System Messages (系统消息)
				if (status == 0xFF) { // Meta Event (元事件)
					if (it == end) break;

					unsigned char metaType = *it++;
					// 检查是否有足够的数据读取length
					if (it == end) break;

					unsigned int length = readVariableLength(it);

					if (metaType == 0x51 && length == 3) { // Set Tempo (设置节拍)
						// 检查是否有足够的数据读取tempo
						if (std::distance(it, end) < 3) break;

						unsigned int tempo =
							(static_cast<unsigned int>(it[0]) << 16) |
							(static_cast<unsigned int>(it[1]) << 8) |
							static_cast<unsigned int>(it[2]);

						// 更新BPM (microseconds per quarter note)
						if (tempo > 0) {
							int newBpm = static_cast<int>(60000000.0 / tempo);
							std::cout << "更新BPM: " << bpm << " -> " << newBpm
								<< " (tempo: " << tempo << "μs/qnote)" << std::endl;
							bpm = newBpm;
						}
					}

					// 跳过剩余数据
					for (unsigned int i = 0; i < length && it != end; ++i) {++it;}
				}
				else if (status == 0xF0 || status == 0xF7) { // SysEx (系统独占消息)
					if (it == end) break;
					unsigned int length = readVariableLength(it);
					// 跳过SysEx数据
					for (unsigned int i = 0; i < length && it != end; ++i) {++it;}
				}
				else {++it;}  // 其他系统信息
				break;
			}
			default:
				// 未知事件类型, 跳过处理
				std::cout << "未知事件类型: 0x" << std::hex << static_cast<int>(eventType) << std::dec << std::endl;
				break;
			}
		}
	}

	// 读取32位大端序整数
	unsigned int readBigEndian32(const char* bytes) {
		return (static_cast<unsigned char>(bytes[0]) << 24) |
			(static_cast<unsigned char>(bytes[1]) << 16) |
			(static_cast<unsigned char>(bytes[2]) << 8) |
			static_cast<unsigned char>(bytes[3]);
	}

	// 读取16位大端序整数
	unsigned int readBigEndian16(const char* bytes) {
		return (static_cast<unsigned char>(bytes[0]) << 8) |
			static_cast<unsigned char>(bytes[1]);
	}

	// 解析MIDI文件
	bool parseMidiFile(const std::string& filename) {
		std::cout << "正在解析MIDI文件: " << filename << std::endl;

		// 打开文件
		FILE* file = nullptr;
		fopen_s(&file, filename.c_str(), "rb");
		if (!file) {
			std::cerr << "无法打开文件: " << filename << std::endl;
			return false;
		}

		// 读取MIDI文件头
		char header[14];
		if (fread(header, 1, 14, file) != 14) {
			std::cerr << "读取MIDI文件头失败!" << std::endl;
			fclose(file);
			return false;
		}

		// 检查"MThd"标识
		if (memcmp(header, "MThd", 4) != 0) {
			std::cerr << "不是有效的MIDI文件!" << std::endl;
			fclose(file);
			return false;
		}

		// 获取格式信息
		unsigned int headerLength = readBigEndian32(header + 4);
		unsigned int format = readBigEndian16(header + 8);
		unsigned int trackCount = readBigEndian16(header + 10);
		int division = static_cast<short>(readBigEndian16(header + 12));

		std::cout << "文件头长度: " << headerLength << std::endl;
		std::cout << "MIDI格式: " << format << std::endl;
		std::cout << "轨道数: " << trackCount << std::endl;
		std::cout << "时间分割: " << division << std::endl;

		// 检查division值
		if (division == 0) {
			std::cerr << "时间分割值不能为 0!" << std::endl;
			fclose(file);
			return false;
		}

		// 初始化默认BPM
		bpm = 120;
		std::cout << "初始BPM: " << bpm << std::endl;

		// 清空并初始化数据
		channels.clear();
		channels.resize(16);
		allTracks.clear();
		finalChannelInstruments.assign(16, 0);

		// 遍历解析每个轨道
		for (unsigned int trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
			char trackHeader[8];
			if (fread(trackHeader, 1, 8, file) != 8) {
				std::cerr << "读取轨道头失败!" << std::endl;
				fclose(file);
				return false;
			}

			// 读取轨道长度 (4字节)
			unsigned int trackLength = readBigEndian32(trackHeader + 4);

			// 检查"MTrk"标识
			if (memcmp(trackHeader, "MTrk", 4) != 0) {
				std::cout << "警告: “轨道 " << trackIndex + 1 << "”不是标准MTrk轨道, 跳过..." << std::endl;
				// 跳过这个轨道的数据
				fseek(file, trackLength, SEEK_CUR);
				continue;
			}

			std::cout << "“轨道 " << trackIndex + 1 << "”长度: " << trackLength << " 字节" << std::endl;

			// 检查轨道长度
			if (trackLength == 0) {
				std::cout << "警告: “轨道 " << trackIndex + 1 << "”长度为0, 跳过..." << std::endl;
				continue;
			}

			if (trackLength > 50000000) {
				std::cerr << "轨道长度过大: " << trackLength << ", 可能文件已损坏" << std::endl;
				fclose(file);
				return false;
			}

			// 读取轨道数据
			std::vector<unsigned char> trackData;
			try {
				trackData.resize(trackLength);
				size_t bytesRead = fread(trackData.data(), 1, trackLength, file);
				if (bytesRead != trackLength) {
					std::cerr << "读取轨道数据不完整! 应该读取: " << trackLength
						<< " 实际读取: " << bytesRead << std::endl;
					trackData.resize(bytesRead);
				}
			}
			catch (const std::bad_alloc& e) {
				std::cerr << "内存分配失败, 轨道长度: " << trackLength << " 错误: " << e.what() << std::endl;
				fclose(file);
				return false;
			}
			catch (const std::exception& e) {
				std::cerr << "创建轨道数据时发生异常: " << e.what() << std::endl;
				fclose(file);
				return false;
			}

			// 为当前轨道创建新的通道数据
			TrackData currentTrack(trackIndex);
			currentTrack.channels.resize(16);
			currentTrack.channelInstruments.assign(16, 0); // 初始化所有通道乐器为0 (Acoustic Grand Piano)

			// 解析当前轨道的事件
			parseTrackEventsForTrack(trackData, division, currentTrack);

			// 将当前轨道添加到所有轨道列表中
			allTracks.push_back(currentTrack);

			std::cout << "解析完“轨道 " << trackIndex + 1 << "”后, 当前 BPM: " << bpm << std::endl;
		}

		fclose(file);

		// 统计信息
		int validChannels = 0;
		int totalNotes = 0;
		int totalChords = 0;
		int totalSingleNotes = 0;  // 单音总数

		std::cout << "\n通道详细信息: " << std::endl;
		std::cout << "================" << std::endl;

		// 统计所有轨道中每个通道的数据
		for (int channel = 0; channel < 16; ++channel) {
			int chordCount = 0;
			int singleNoteCount = 0;
			int noteCount = 0;
			for (const auto& track : allTracks) {
				for (const auto& sound : track.channels[channel]) {
					if (sound.isSingleNote()) {singleNoteCount++;}
					else if (sound.isChord()) {chordCount++;}
					noteCount += sound.notes.size();
				}
			}
			totalNotes += noteCount;
			totalChords += chordCount;
			totalSingleNotes += singleNoteCount;

			// 判断通道是否有效
			if (chordCount > 0 || singleNoteCount > 0 || noteCount > 0) {
				std::cout << "通道 " << channel + 1 << ": 单音数: " << singleNoteCount << ", 和弦数: " << chordCount << ", 总音符数: " << noteCount;
				std::cout << " [有效]" << std::endl;
				validChannels++;
			}
		}

		std::cout << "\n统计结果: " << std::endl;
		std::cout << "===========" << std::endl;
		std::cout << "总通道数: 16" << std::endl;
		std::cout << "有效通道数 (包含音符): " << validChannels << std::endl;
		std::cout << "轨道数: " << allTracks.size() << std::endl;
		std::cout << "总单音数: " << totalSingleNotes << std::endl;
		std::cout << "总和弦数: " << totalChords << std::endl;
		std::cout << "总音符数: " << totalNotes << std::endl;
		std::cout << "最终BPM: " << bpm << std::endl;

		return true;
	}

	// 解析单个轨道事件 (用于新轨道数据结构)
	void parseTrackEventsForTrack(const std::vector<unsigned char>& trackData, int division, TrackData& track) {
		auto it = trackData.begin();
		auto end = trackData.end();

		unsigned int currentTime = 0;
		unsigned char lastStatus = 0;

		while (it != end) {
			// 检查是否有足够的数据读取delta time
			if (it == end) break;

			// 读取 delta time
			unsigned int deltaTime = readVariableLength(it);
			currentTime += deltaTime;

			// 检查是否有足够的数据读取状态字节
			if (it == end) break;

			// 读取状态字节
			unsigned char status = *it++;
			if ((status & 0x80) == 0) {
				// 运行状态 - 使用上一个状态
				if (it != trackData.begin()) {
					it--; // 回退一位
					status = lastStatus;
				}
				else {
					break;
				}
			}
			else {lastStatus = status;}

			unsigned char eventType = status & 0xF0;  // 高4位是事件类型
			int currentChannel = status & 0x0F;       // 低4位是通道号

			switch (eventType) {
			case 0x80: // Note Off
			case 0x90: { // Note On
				// 检查是否有足够的数据读取note和velocity
				if (std::distance(it, end) < 2) break;

				unsigned char note = *it++;
				unsigned char velocity = *it++;

				// 处理音符开启/关闭事件, Note On力度为0等同于Note Off
				if (eventType == 0x90 && velocity == 0) {}  // 简化处理，当作Note Off
				
				else if (eventType == 0x90) {
					// 计算时间 (毫秒)
					double timeInMs = calculateTimeInMs(currentTime, division);

					// 创建新音符
					Note newNote;
					newNote.noteNumber = note;
					newNote.startTime = timeInMs;
					newNote.endTime = timeInMs + 500; // 默认持续时间

					// 添加到对应通道
					if (currentChannel < (int)track.channels.size()) {
						// 将同时刻的音符记为同一个音处理
						if (track.channels[currentChannel].empty() ||
							track.channels[currentChannel].back().startTime != timeInMs) {
							Sound newSound;
							newSound.startTime = timeInMs;
							newSound.notes.push_back(newNote);
							track.channels[currentChannel].push_back(newSound);
						}
						else {track.channels[currentChannel].back().notes.push_back(newNote);}
					}
				}
				break;
			}
			case 0xA0: // Polyphonic Key Pressure (多键触后)
			case 0xB0: // Control Change (控制改变)
				// 跳过2个数据字节
				if (std::distance(it, end) < 2) break;
				it += 2;
				break;
			case 0xC0: // Program Change (程序改变) - 乐器切换
				{
					// Program Change 只有1个数据字节
					if (std::distance(it, end) < 1) break;
					unsigned char programNumber = *it++;
					
					// 更新轨道中通道的乐器
					if (currentChannel < (int)track.channelInstruments.size()) {
						track.channelInstruments[currentChannel] = programNumber;
						std::cout << "轨道 " << track.trackIndex + 1 << ", 通道 " << currentChannel + 1 
							<< "，乐器变更为: " << static_cast<int>(programNumber) + 1 << ". " << getInstrumentName(programNumber) << std::endl;
					}
					break;
				}
			case 0xD0: // Channel Pressure (通道压力)
				// 跳过1个数据字节
				if (std::distance(it, end) < 1) break;
				++it;
				break;
			case 0xE0: { // Pitch Bend (弯音)
				// 跳过2个数据字节
				if (std::distance(it, end) < 2) break;
				it += 2;
				break;
			}
			case 0xF0: { // System Messages (系统消息)
				if (status == 0xFF) { // Meta Event (元事件)
					if (it == end) break;

					unsigned char metaType = *it++;
					// 检查是否有足够的数据读取length
					if (it == end) break;

					unsigned int length = readVariableLength(it);

					if (metaType == 0x51 && length == 3) { // Set Tempo (设置节拍)
						// 检查是否有足够的数据读取tempo
						if (std::distance(it, end) < 3) break;

						unsigned int tempo =
							(static_cast<unsigned int>(it[0]) << 16) |
							(static_cast<unsigned int>(it[1]) << 8) |
							static_cast<unsigned int>(it[2]);

						// 更新BPM (microseconds per quarter note)
						if (tempo > 0) {
							int newBpm = static_cast<int>(60000000.0 / tempo);
							bpm = newBpm;
						}
					}

					// 跳过剩余数据
					for (unsigned int i = 0; i < length && it != end; ++i) {++it;}
				}
				else if (status == 0xF0 || status == 0xF7) { // SysEx (系统独占消息)
					if (it == end) break;
					unsigned int length = readVariableLength(it);
					// 跳过SysEx数据
					for (unsigned int i = 0; i < length && it != end; ++i) {++it;}
				}
				else {++it;}  // 其他系统信息
				break;
			}
			default:break;}  // 未知事件类型, 跳过处理
		}
	}

	// 将轨道中的音符按起始时间分组为音
	void groupTrackNotesIntoSounds(TrackData& track) {
		// 为每个通道重新整理音符
		for (auto& channel : track.channels) {
			// 临时存储所有音符
			std::vector<Note> allNotes;
			for (const auto& sound : channel) {
				for (const auto& note : sound.notes) {
					allNotes.push_back(note);
				}
			}

			// 清空当前通道
			channel.clear();

			// 按起始时间排序
			std::sort(allNotes.begin(), allNotes.end(),
				[](const Note& a, const Note& b) {
				return a.startTime < b.startTime;
			});

			// 将相同起始时间的音符合并为音
			if (!allNotes.empty()) {
				Sound currentSound;  // 当前音
				currentSound.startTime = allNotes[0].startTime;
				currentSound.notes.push_back(allNotes[0]);

				for (size_t i = 1; i < allNotes.size(); ++i) {
					// 如果起始时间相同 (1ms差值内), 添加到当前音
					if (std::abs(allNotes[i].startTime - currentSound.startTime) < 1.0) {
						currentSound.notes.push_back(allNotes[i]);
					}
					else {
						// 否则保存当前音, 开始新音
						channel.push_back(currentSound);
						currentSound = Sound();
						currentSound.startTime = allNotes[i].startTime;
						currentSound.notes.push_back(allNotes[i]);
					}
				}
				// 添加最后一个音
				channel.push_back(currentSound);
			}
		}
	}

	// 实际应用中这里会根据真实MIDI数据进行分组
	void groupNotesIntoSounds() {
		std::cout << "正在重新整理音..." << std::endl;

		// 为每个通道重新整理音符
		for (auto& channel : channels) {
			// 临时存储所有音符
			std::vector<Note> allNotes;
			for (const auto& sound : channel) {
				for (const auto& note : sound.notes) {
					allNotes.push_back(note);
				}
			}

			// 清空当前通道
			channel.clear();

			// 按起始时间排序
			std::sort(allNotes.begin(), allNotes.end(),
				[](const Note& a, const Note& b) {
				return a.startTime < b.startTime;
			});

			// 将相同起始时间的音符合并为音
			if (!allNotes.empty()) {
				Sound currentSound;
				currentSound.startTime = allNotes[0].startTime;
				currentSound.notes.push_back(allNotes[0]);

				for (size_t i = 1; i < allNotes.size(); ++i) {
					// 如果起始时间相同 (1ms差值内), 添加到当前音
					if (std::abs(allNotes[i].startTime - currentSound.startTime) < 1.0) {
						currentSound.notes.push_back(allNotes[i]);
					}
					else {
						// 否则保存当前音, 开始新音
						channel.push_back(currentSound);
						currentSound = Sound();
						currentSound.startTime = allNotes[i].startTime;
						currentSound.notes.push_back(allNotes[i]);
					}
				}
				// 添加最后一个音
				channel.push_back(currentSound);
			}
		}
	}

	// 获取乐器名称的辅助函数
	std::string getInstrumentName(int programNumber) {
		static const std::vector<std::string> gmInstruments = {
			"Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
			"Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavi",
			"Celesta", "Glockenspiel", "Music Box", "Vibraphone",
			"Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
			"Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
			"Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
			"Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)",
			"Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar harmonics",
			"Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass",
			"Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
			"Violin", "Viola", "Cello", "Contrabass",
			"Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
			"String Ensemble 1", "String Ensemble 2", "SynthStrings 1", "SynthStrings 2",
			"Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
			"Trumpet", "Trombone", "Tuba", "Muted Trumpet",
			"French Horn", "Brass Section", "SynthBrass 1", "SynthBrass 2",
			"Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
			"Oboe", "English Horn", "Bassoon", "Clarinet",
			"Piccolo", "Flute", "Recorder", "Pan Flute",
			"Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
			"Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)",
			"Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
			"Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
			"Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep",
			"FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
			"FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
			"Sitar", "Banjo", "Shamisen", "Koto",
			"Kalimba", "Bag pipe", "Fiddle", "Shanai",
			"Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
			"Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
			"Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
			"Telephone Ring", "Helicopter", "Applause", "Gunshot"
		};
		
		if (programNumber >= 0 && programNumber < static_cast<int>(gmInstruments.size())) {
			return gmInstruments[programNumber];
		}
		return "Unknown Instrument";
	}

	// 判断是否为钢琴类乐器 (键盘乐器)
	bool isPianoInstrument(int programNumber) {
		// 钢琴类乐器: 0~7 (Acoustic Grand Piano到Clavi)
		// 以及一些其他键盘乐器: 8~15 (Celesta到Dulcimer)
		return (programNumber >= 0 && programNumber <= 15);
	}

	// 判断轨道中的通道是否有效
	bool isTrackChannelValid(const std::vector<Sound>& channelData) {
		if (channelData.empty()) return false;
		int noteCount = 0;
		for (const auto& sound : channelData) {
			noteCount += sound.notes.size();
		}
		return (channelData.size() > 0 || noteCount > 0);
	}

	// 计算单个音的难度
	double calculateSoundDifficulty(const Sound& sound, double basicDifficulty, bool isFirstSound = false) {
		if (sound.notes.empty()) return 0;
		
		double noteCountDifficulty = calculateNoteCountDifficulty(sound.notes.size());
		double spanDifficulty = calculateSpanDifficulty(sound.notes);

		// 如果是第一个音 (首音)
		if (isFirstSound) {
			// 第一个音的基础难度已经设为0, 所以不需要考虑basicDifficulty
			if (sound.isSingleNote()) {
				// 第一个音为单音时: 只有音数难度
				return noteCountDifficulty;
			}
			else if (sound.isChord()) {
				// 第一个音为和弦时: 音数难度和跨度难度的平均值
				if (spanDifficulty > 0) {
					// 有跨度的和弦: 取两项平均值
					return (noteCountDifficulty + spanDifficulty) / 2.0;
				} else {
					// 无跨度的和弦: 只有音数难度
					return noteCountDifficulty;  // 理论上不会发生
				}
			}
		}
		// 如果不是第一个音 (后续音)
		else {
			// 单音 (单个音符)
			if (sound.isSingleNote()) {
				// 单音没有跨度难度, 所以取基础难度和音数难度的平均值
				return (basicDifficulty + noteCountDifficulty) / 2.0;
			}
			// 和弦 (多个音符)
			else if (sound.isChord()) {
				if (spanDifficulty > 0) {
					// 有跨度的和弦: 取三项平均值
					return (basicDifficulty + noteCountDifficulty + spanDifficulty) / 3.0;
				} else {
					// 无跨度的和弦: 取两项平均值
					return (basicDifficulty + noteCountDifficulty) / 2.0;  // 理论上不会发生
				}
			}
		}
		return 0;
	}

	// 计算单个通道在指定数据中的难度
	double calculateChannelDifficultyFromData(const std::vector<Sound>& channelSounds) {
		// 检查通道是否有数据
		if (channelSounds.empty()) {return 0;}

		double totalDifficulty = 0; // 总难度

		// 处理第一个音
		if (!channelSounds.empty()) {
			// 第一个音的基础难度为0, 并标记为第一个音
			double firstSoundDifficulty = calculateSoundDifficulty(channelSounds[0], 0.0, true);
			totalDifficulty += firstSoundDifficulty;
		}

		// 从第二个音开始计算
		for (size_t i = 1; i < channelSounds.size(); ++i) {
			double timeInterval = channelSounds[i].startTime - channelSounds[i - 1].startTime;

			// 只有时间间隔为正, 才计算基础难度
			double basicDifficulty = 0.0;
			if (timeInterval > 0) {basicDifficulty = calculateBasicDifficulty(timeInterval);}
			
			// 不是第一个音, 所以isFirstSound为false
			double soundDifficulty = calculateSoundDifficulty(channelSounds[i], basicDifficulty, false);
			totalDifficulty += soundDifficulty;
		}

		// 返回通道的平均难度 (总难度除以音的数量，再除以6还原为真实难度值)
		return totalDifficulty / static_cast<double>(channelSounds.size()) / 6.0;
	}

	// 计算基础难度 (基于时间间隔)
	double calculateBasicDifficulty(double timeIntervalMs) {
		// 公式: 6000 / 时间间隔毫秒数
		// 需要将100ms间隔对应“十级难度” (值为60), 故用6000来除 (6倍放大)
		if (timeIntervalMs <= 0) return 0;
		return 6000.0 / timeIntervalMs;  // 时间间隔越短, 速度越快, 难度越高
	}

	// 计算音数难度
	double calculateNoteCountDifficulty(int noteCount) {
		// 1个音符难度为2, 5个音符难度为10 (原始难度)
		// 1个音符难度为12, 5个音符难度为60 (6倍放大)
		return noteCount * 12;
	}

	// 计算跨度难度
	double calculateSpanDifficulty(const std::vector<Note>& notes) {
		// 1个半音的跨度难度为5/6, 12个半音 (一个八度) 的跨度难度为10
		// 1个半音的跨度难度为5, 12个半音 (一个八度) 的跨度难度为60 (6倍放大)

		// 如果没有音符或仅由一个音符构成的音 (单音), 跨度难度为0
		if (notes.empty() || notes.size() == 1) return 0;

		// 找出根音和冠音
		int minNote = notes[0].noteNumber;
		int maxNote = notes[0].noteNumber;

		for (const auto& note : notes) {
			minNote = std::min(minNote, note.noteNumber);
			maxNote = std::max(maxNote, note.noteNumber);
		}

		int span = maxNote - minNote;
		// 跨度难度 = 音程跨度 * 5 (6倍放大)
		return span * 5;
	}

	// 计算单个通道的难度
	double calculateChannelDifficulty(int channelIndex);

	// 计算单个音轨的难度
	double calculateTrackDifficulty(const std::vector<Sound>& trackChannel) {
		// 直接调用通道难度计算函数
		return calculateChannelDifficultyFromData(trackChannel);
	}

	// 计算整体难度 (按乐器和通道显示, 支持钢琴多音轨)
	double calculateOverallDifficulty() {
		if (allTracks.empty()) return 0;

		std::cout << "\n各通道难度计算:" << std::endl;
		std::cout << "========================" << std::endl;

		// 首先确定每个通道的最终乐器
		std::vector<int> channelFinalInstruments(16, 0);
		for (const auto& track : allTracks) {
		 for (int channel = 0; channel < 16; ++channel) {
			 // 使用最后一个非零的乐器设置, 或者如果一直是0但有数据则保持为0
			 if (track.channelInstruments[channel] != 0 || isTrackChannelValid(track.channels[channel])) {
				 channelFinalInstruments[channel] = track.channelInstruments[channel];
			 }
		 }
	 }

	 double totalDifficulty = 0;  // 总难度
	 int validChannelCount = 0;   // 有效通道计数

	 // 为每个通道处理数据
	 for (int channel = 0; channel < 16; ++channel) {
		 // 收集该通道在所有音轨中的有效数据
		 std::vector<std::vector<Sound>> validTrackChannels;
		 std::vector<int> validTrackIndices;  // 记录对应的音轨索引用于显示
		 
		 for (size_t trackIdx = 0; trackIdx < allTracks.size(); ++trackIdx) {
			 if (isTrackChannelValid(allTracks[trackIdx].channels[channel])) {
				 validTrackChannels.push_back(allTracks[trackIdx].channels[channel]);
				 validTrackIndices.push_back(static_cast<int>(trackIdx));
			 }
		 }

		 // 如果该通道没有任何有效数据, 则跳过
		 if (validTrackChannels.empty()) {
			 continue;
		 }

		 bool isPiano = isPianoInstrument(channelFinalInstruments[channel]);
		 
		 if (validTrackChannels.size() > 1) {
			 // 多条有效音轨 - 所有音轨难度的平均值 (无论是钢琴还是非钢琴类乐器)
			 std::cout << "通道 " << channel + 1 << ": " << channelFinalInstruments[channel] + 1 
				 << ". " << getInstrumentName(channelFinalInstruments[channel]) << std::endl;
			 
			 double channelTotalDifficulty = 0;  // 通道的总难度
			 for (size_t i = 0; i < validTrackChannels.size(); ++i) {
					// 计算当前音轨的难度
					double difficulty = calculateTrackDifficulty(validTrackChannels[i]);
					channelTotalDifficulty += difficulty;

					// 统计单音数、和弦数和音符数用于显示
					int chordCount = 0, singleNoteCount = 0, noteCount = 0;
					for (const auto& sound : validTrackChannels[i]) {
						if (sound.isChord()) chordCount++;
						else if (sound.isSingleNote()) singleNoteCount++;
						noteCount += sound.notes.size();
					}
						
					std::cout << "  “音轨 " << validTrackIndices[i] + 1 << "”难度: " << std::fixed << std::setprecision(2)
						<< difficulty << " (单音数: " << singleNoteCount << ", 和弦数: " << chordCount
						<< ", 音符数: " << noteCount << ")" << std::endl;
			 }
			 
			 // 计算所有音轨的平均值
			 double channelAverageDifficulty = channelTotalDifficulty / validTrackChannels.size();
			 std::cout << "“通道 " << channel + 1 << "”的平均难度: " << std::fixed << std::setprecision(2)
				 << channelAverageDifficulty << std::endl;
			 totalDifficulty += channelAverageDifficulty;
			 validChannelCount++;  // 增加有效通道计数
		 }
		 else {
			 // 单音轨情况, 直接计算该音轨的难度
			 double channelDifficulty = calculateChannelDifficultyFromData(validTrackChannels[0]);
			 std::cout << "通道 " << channel + 1 << ": " << channelFinalInstruments[channel] + 1 
				 << ". " << getInstrumentName(channelFinalInstruments[channel]) 
				 << "，难度: " << std::fixed << std::setprecision(2) << channelDifficulty;
			 
			 std::cout << std::endl;
			 
			 totalDifficulty += channelDifficulty;
			 validChannelCount++;  // 增加有效通道计数
		 }
	 }

	 // 如果没有有效通道, 返回0
	 if (validChannelCount == 0) {
		 std::cout << "没有有效通道, 总难度: 0" << std::endl;
		 return 0;
	 }

	 // 返回所有有效通道的平均难度
	 double overallDifficulty = totalDifficulty / validChannelCount;
	 std::cout << "\nMIDI总难度: " << std::fixed << std::setprecision(2) << overallDifficulty << std::endl;

	 return overallDifficulty;
	}
};

int main() {
	std::cout << "MIDI难度计算" << std::endl;
	std::cout << "============" << std::endl;

	// 创建计算器实例
	MidiDifficultyCalculator calculator;

	// 打开文件选择对话框
	std::string midiFilePath = calculator.openFileSelector();

	if (midiFilePath.empty()) {
		std::cout << "未选择MIDI文件或选择失败。" << std::endl;
		system("pause");
		return 1;
	}

	std::cout << "选择的MIDI文件: " << midiFilePath << std::endl;

	// 解析 MIDI 文件
	if (!calculator.parseMidiFile(midiFilePath)) {
		std::cerr << "解析MIDI文件失败!" << std::endl;
		system("pause");
		return 1;
	}

	// 计算并显示结果
	double overallDifficulty = calculator.calculateOverallDifficulty();

	std::cout << "\n最终结果:" << std::endl;
	std::cout << "============" << std::endl;
	std::cout << "MIDI难度: " << std::fixed << std::setprecision(2)
		<< overallDifficulty << std::endl;

	// 难度等级判断
	std::cout << "\n难度等级: ";
	if (overallDifficulty >= 8) {
		std::cout << "专业级 (8~10)" << std::endl;
	}
	else if (overallDifficulty >= 6) {
		std::cout << "高级 (6~8)" << std::endl;
	}
	else if (overallDifficulty >= 4) {
		std::cout << "中级 (4~6)" << std::endl;
	}
	else if (overallDifficulty >= 2) {
		std::cout << "初级 (2~4)" << std::endl;
	}
	else {
		std::cout << "入门级 (0~2)" << std::endl;
	}

	system("pause");
	return 0;
}