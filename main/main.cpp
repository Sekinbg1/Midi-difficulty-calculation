#define NOMINMAX  // 禁用Windows头文件中的min和max宏定义，避免与标准库的std::min和std::max冲突
#include <iostream>     // 输入输出流
#include <vector>       // 向量容器
#include <algorithm>    // 算法库 (用于排序、最大最小值等)
#include <iomanip>      // IO 流格式控制 (如设置小数位数)
#include <windows.h>    // Windows API
#include <commdlg.h>    // 通用对话框 (文件选择对话框)
#include <string>       // 字符串类

class MidiDifficultyCalculator {  // 定义一个 MIDI 难度计算器类

	// 音符结构体
	struct Note {
		int noteNumber;     // Midi 音符编号 (0~127)，表示音高
		double startTime;   // 开始时间 (毫秒)
		double endTime;     // 结束时间 (毫秒)
	};

	// 和弦结构体 (组合音)
	struct Chord {
		std::vector<Note> notes;  // 和弦包含多个音符
		double startTime;         // 和弦开始时间
	};

	// 轨道数据结构
	struct TrackData {
		std::vector<std::vector<Chord>> channels;  // 16个通道的和弦数据
		std::vector<int> channelInstruments;       // 16个通道的当前乐器
		unsigned int trackIndex;                   // 轨道索引
		
		TrackData(unsigned int index) : trackIndex(index) {
			channels.resize(16);
			channelInstruments.assign(16, 0); // 默认都是钢琴
		}
		
		// 判断通道是否有效
		bool isChannelValid(int channel) const {
			if (channel < 0 || channel >= (int)channels.size()) return false;
			int chordCount = channels[channel].size();
			int noteCount = 0;
			for (const auto& chord : channels[channel]) {
				noteCount += chord.notes.size();
			}
			return (chordCount > 0 || noteCount > 0);
		}
		
		// 获取通道的和弦数和音符数
		void getChannelStats(int channel, int& chordCount, int& noteCount) const {
			chordCount = channels[channel].size();
			noteCount = 0;
			for (const auto& chord : channels[channel]) {
				noteCount += chord.notes.size();
			}
		}
	};

private:
	int bpm;  // 每分钟节拍数 (速度)
	std::vector<std::vector<Chord> > channels;  // 16 个通道的和弦数据 (用于兼容)
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
		OPENFILENAMEA ofn;  // 使用 ANSI 版本的 Windows 文件对话框结构体
		char szFile[260] = { 0 };  // 存储文件路径的缓冲区 (使用char)

		ZeroMemory(&ofn, sizeof(ofn));  // 初始化结构体
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = szFile;  // 指定文件路径缓冲区
		ofn.nMaxFile = sizeof(szFile);  // ANSI 版本，直接使用 sizeof
		ofn.lpstrFilter = "MIDI文件\0*.mid;*.midi\0所有文件\0*.*\0";  // 文件类型过滤器
		ofn.nFilterIndex = 1;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;  // 对话框标志

		if (GetOpenFileNameA(&ofn)) {  // 使用 ANSI 版本的 API
			return std::string(szFile);  // 直接返回选中的文件路径
		}
		return "";  // 如果没有选择文件，则返回空字符串
	}

	// 辅助函数：解析可变长度值
	unsigned int readVariableLength(std::vector<unsigned char>::const_iterator& it) {
		unsigned int value = 0;
		unsigned char byte;
		int count = 0; // 防止无限循环

		do {
			if (count >= 4) { // 可变长度值最多 4 个字节
				break;
			}
			byte = *it++;  // 读取一个字节并移动迭代器
			value = (value << 7) | (byte & 0x7F);  // 拼接可变长度值
			count++;
		} while ((byte & 0x80) && count < 4);  // MSB 为 1 表示还有后续字节

		return value;
	}

	// 计算时间 (毫秒)
	double calculateTimeInMs(unsigned int ticks, int division) {
		// 处理负 division (SMPTE 格式)
		if (division < 0) {
			// 负数表示 SMPTE 格式：-frames_per_second * ticks_per_frame
			int smpte_fps = -(division >> 8);
			int ticks_per_frame = division & 0xFF;

			// 简化处理，假设 30fps
			if (smpte_fps != 0 && ticks_per_frame != 0) {
				double seconds = static_cast<double>(ticks) / (abs(smpte_fps) * ticks_per_frame);
				return seconds * 1000.0;
			}
			return 0.0;
		}
		else {
			// 正数表示 ticks per quarter note
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
			// 检查是否有足够的数据读取 delta time
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

			unsigned char eventType = status & 0xF0;  // 高 4 位是事件类型
			int currentChannel = status & 0x0F;       // 低 4 位是通道号

			switch (eventType) {
			case 0x80: // Note Off
			case 0x90: { // Note On
				// 检查是否有足够的数据读取 note 和 velocity
				if (std::distance(it, end) < 2) break;

				unsigned char note = *it++;
				unsigned char velocity = *it++;

				// 处理音符开启/关闭事件，Note On 力度为 0 等同于 Note Off
				if (eventType == 0x90 && velocity == 0) {}  // 简化处理，当作 Note Off
				
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
						// 为了简化，将同时刻的音符记为和弦处理
						if (channels[currentChannel].empty() ||
							channels[currentChannel].back().startTime != timeInMs) {
							Chord newChord;
							newChord.startTime = timeInMs;
							newChord.notes.push_back(newNote);
							channels[currentChannel].push_back(newChord);
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
					// 检查是否有足够的数据读取 length
					if (it == end) break;

					unsigned int length = readVariableLength(it);

					if (metaType == 0x51 && length == 3) { // Set Tempo (设置节拍)
						// 检查是否有足够的数据读取 tempo
						if (std::distance(it, end) < 3) break;

						unsigned int tempo =
							(static_cast<unsigned int>(it[0]) << 16) |
							(static_cast<unsigned int>(it[1]) << 8) |
							static_cast<unsigned int>(it[2]);

						// 更新 BPM (microseconds per quarter note)
						if (tempo > 0) {
							int newBpm = static_cast<int>(60000000.0 / tempo);
							std::cout << "更新 BPM: " << bpm << " -> " << newBpm
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
					// 跳过 SysEx 数据
					for (unsigned int i = 0; i < length && it != end; ++i) {++it;}
				}
				else {++it;}  // 其他系统信息
				break;
			}
			default:
				// 未知事件类型，跳过处理
				std::cout << "未知事件类型: 0x" << std::hex << static_cast<int>(eventType) << std::dec << std::endl;
				break;
			}
		}
	}

	// 读取 32 位大端序整数
	unsigned int readBigEndian32(const char* bytes) {
		return (static_cast<unsigned char>(bytes[0]) << 24) |
			(static_cast<unsigned char>(bytes[1]) << 16) |
			(static_cast<unsigned char>(bytes[2]) << 8) |
			static_cast<unsigned char>(bytes[3]);
	}

	// 读取 16 位大端序整数
	unsigned int readBigEndian16(const char* bytes) {
		return (static_cast<unsigned char>(bytes[0]) << 8) |
			static_cast<unsigned char>(bytes[1]);
	}

	// 解析 Midi 文件
	bool parseMidiFile(const std::string& filename) {
		std::cout << "正在解析 Midi 文件: " << filename << std::endl;

		// 打开文件
		FILE* file = nullptr;
		fopen_s(&file, filename.c_str(), "rb");
		if (!file) {
			std::cerr << "无法打开文件: " << filename << std::endl;
			return false;
		}

		// 读取 Midi 文件头
		char header[14];
		if (fread(header, 1, 14, file) != 14) {
			std::cerr << "读取 Midi 文件头失败！" << std::endl;
			fclose(file);
			return false;
		}

		// 检查"MThd"标识
		if (memcmp(header, "MThd", 4) != 0) {
			std::cerr << "不是有效的 Midi 文件！" << std::endl;
			fclose(file);
			return false;
		}

		// 获取格式信息
		unsigned int headerLength = readBigEndian32(header + 4);
		unsigned int format = readBigEndian16(header + 8);
		unsigned int trackCount = readBigEndian16(header + 10);
		int division = static_cast<short>(readBigEndian16(header + 12));

		std::cout << "文件头长度: " << headerLength << std::endl;
		std::cout << "Midi 格式: " << format << std::endl;
		std::cout << "轨道数: " << trackCount << std::endl;
		std::cout << "时间分割: " << division << std::endl;

		// 检查 division 值
		if (division == 0) {
			std::cerr << "时间分割值不能为 0！" << std::endl;
			fclose(file);
			return false;
		}

		// 初始化默认 BPM
		bpm = 120;
		std::cout << "初始 BPM: " << bpm << std::endl;

		// 清空并初始化数据
		channels.clear();
		channels.resize(16);
		allTracks.clear();
		finalChannelInstruments.assign(16, 0);

		// 遍历解析每个轨道
		for (unsigned int trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
			char trackHeader[8];
			if (fread(trackHeader, 1, 8, file) != 8) {
				std::cerr << "读取轨道头失败！" << std::endl;
				fclose(file);
				return false;
			}

			// 读取轨道长度 (4 字节)
			unsigned int trackLength = readBigEndian32(trackHeader + 4);

			// 检查"MTrk"标识
			if (memcmp(trackHeader, "MTrk", 4) != 0) {
				std::cout << "警告：“轨道 " << trackIndex + 1 << "”不是标准 MTrk 轨道，跳过..." << std::endl;
				// 跳过这个轨道的数据
				fseek(file, trackLength, SEEK_CUR);
				continue;
			}

			std::cout << "“轨道 " << trackIndex + 1 << "”长度：" << trackLength << " 字节" << std::endl;

			// 检查轨道长度
			if (trackLength == 0) {
				std::cout << "警告：“轨道 " << trackIndex + 1 << "”长度为 0，跳过..." << std::endl;
				continue;
			}

			if (trackLength > 50000000) {
				std::cerr << "轨道长度过大：" << trackLength << "，可能文件已损坏" << std::endl;
				fclose(file);
				return false;
			}

			// 读取轨道数据
			std::vector<unsigned char> trackData;
			try {
				trackData.resize(trackLength);
				size_t bytesRead = fread(trackData.data(), 1, trackLength, file);
				if (bytesRead != trackLength) {
					std::cerr << "读取轨道数据不完整！ 应该读取：" << trackLength
						<< " 实际读取：" << bytesRead << std::endl;
					trackData.resize(bytesRead);
				}
			}
			catch (const std::bad_alloc& e) {
				std::cerr << "内存分配失败，轨道长度：" << trackLength << " 错误：" << e.what() << std::endl;
				fclose(file);
				return false;
			}
			catch (const std::exception& e) {
				std::cerr << "创建轨道数据时发生异常：" << e.what() << std::endl;
				fclose(file);
				return false;
			}

			// 为当前轨道创建新的通道数据
			TrackData currentTrack(trackIndex);
			currentTrack.channels.resize(16);
			currentTrack.channelInstruments.assign(16, 0); // 初始化所有通道乐器为 0 (Acoustic Grand Piano)

			// 解析当前轨道的事件
			parseTrackEventsForTrack(trackData, division, currentTrack);

			// 将当前轨道添加到所有轨道列表中
			allTracks.push_back(currentTrack);

			std::cout << "解析完“轨道 " << trackIndex + 1 << "”后, 当前 BPM：" << bpm << std::endl;
		}

		fclose(file);

		// 统计信息
		int validChannels = 0;
		int totalNotes = 0;
		int totalChords = 0;

		std::cout << "\n通道详细信息：" << std::endl;
		std::cout << "================" << std::endl;

		// 统计所有轨道中每个通道的数据
		for (int channel = 0; channel < 16; ++channel) {
			int chordCount = 0;
			int noteCount = 0;
			for (const auto& track : allTracks) {
				for (const auto& chord : track.channels[channel]) {
					chordCount++;
					noteCount += chord.notes.size();
				}
			}
			totalNotes += noteCount;
			totalChords += chordCount;

			// 判断通道是否有效
			if (chordCount > 0 || noteCount > 0) {
				std::cout << "通道 " << channel + 1 << "：一共有" << chordCount << " 个和弦, " << noteCount << " 个音符";
				std::cout << " [有效]" << std::endl;
				validChannels++;
			}
		}

		std::cout << "\n统计结果：" << std::endl;
		std::cout << "===========" << std::endl;
		std::cout << "总通道数：16" << std::endl;
		std::cout << "有效通道数 (包含和弦或音符)：" << validChannels << std::endl;
		std::cout << "轨道数：" << allTracks.size() << std::endl;
		std::cout << "总和弦数：" << totalChords << std::endl;
		std::cout << "总音符数：" << totalNotes << std::endl;
		std::cout << "最终 BPM：" << bpm << std::endl;

		return true;
	}

	// 解析单个轨道事件 (用于新轨道数据结构)
	void parseTrackEventsForTrack(const std::vector<unsigned char>& trackData, int division, TrackData& track) {
		auto it = trackData.begin();
		auto end = trackData.end();

		unsigned int currentTime = 0;
		unsigned char lastStatus = 0;

		while (it != end) {
			// 检查是否有足够的数据读取 delta time
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

			unsigned char eventType = status & 0xF0;  // 高 4 位是事件类型
			int currentChannel = status & 0x0F;       // 低 4 位是通道号

			switch (eventType) {
			case 0x80: // Note Off
			case 0x90: { // Note On
				// 检查是否有足够的数据读取 note 和 velocity
				if (std::distance(it, end) < 2) break;

				unsigned char note = *it++;
				unsigned char velocity = *it++;

				// 处理音符开启/关闭事件，Note On 力度为 0 等同于 Note Off
				if (eventType == 0x90 && velocity == 0) {}  // 简化处理，当作 Note Off
				
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
						// 为了简化，将同时刻的音符记为和弦处理
						if (track.channels[currentChannel].empty() ||
							track.channels[currentChannel].back().startTime != timeInMs) {
							Chord newChord;
							newChord.startTime = timeInMs;
							newChord.notes.push_back(newNote);
							track.channels[currentChannel].push_back(newChord);
						}
						else {track.channels[currentChannel].back().notes.push_back(newNote);}
					}
				}
				break;
			}
			case 0xA0: // Polyphonic Key Pressure (多键触后)
			case 0xB0: // Control Change (控制改变)
				// 跳过 2 个数据字节
				if (std::distance(it, end) < 2) break;
				it += 2;
				break;
			case 0xC0: // Program Change (程序改变) - 乐器切换
				{
					// Program Change 只有 1 个数据字节
					if (std::distance(it, end) < 1) break;
					unsigned char programNumber = *it++;
					
					// 更新轨道中通道的乐器
					if (currentChannel < (int)track.channelInstruments.size()) {
						track.channelInstruments[currentChannel] = programNumber;
						std::cout << "轨道 " << track.trackIndex + 1 << "，通道 " << currentChannel + 1 
							<< "，切换为乐器: " << static_cast<int>(programNumber) + 1 << ". " << getInstrumentName(programNumber) << std::endl;
					}
					break;
				}
			case 0xD0: // Channel Pressure (通道压力)
				// 跳过 1 个数据字节
				if (std::distance(it, end) < 1) break;
				++it;
				break;
			case 0xE0: { // Pitch Bend (弯音)
				// 跳过 2 个数据字节
				if (std::distance(it, end) < 2) break;
				it += 2;
				break;
			}
			case 0xF0: { // System Messages (系统消息)
				if (status == 0xFF) { // Meta Event (元事件)
					if (it == end) break;

					unsigned char metaType = *it++;
					// 检查是否有足够的数据读取 length
					if (it == end) break;

					unsigned int length = readVariableLength(it);

					if (metaType == 0x51 && length == 3) { // Set Tempo (设置节拍)
						// 检查是否有足够的数据读取 tempo
						if (std::distance(it, end) < 3) break;

						unsigned int tempo =
							(static_cast<unsigned int>(it[0]) << 16) |
							(static_cast<unsigned int>(it[1]) << 8) |
							static_cast<unsigned int>(it[2]);

						// 更新 BPM (microseconds per quarter note)
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
					// 跳过 SysEx 数据
					for (unsigned int i = 0; i < length && it != end; ++i) {++it;}
				}
				else {++it;}  // 其他系统信息
				break;
			}
			default:
				break;  // 未知事件类型，跳过处理
			}
		}
	}

	// 将轨道中的音符按起始时间分组为和弦
	void groupTrackNotesIntoChords(TrackData& track) {
		// 为每个通道重新整理音符
		for (auto& channel : track.channels) {
			// 临时存储所有音符
			std::vector<Note> allNotes;
			for (const auto& chord : channel) {
				for (const auto& note : chord.notes) {
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

			// 将相同起始时间的音符合并为和弦
			if (!allNotes.empty()) {
				Chord currentChord;
				currentChord.startTime = allNotes[0].startTime;
				currentChord.notes.push_back(allNotes[0]);

				for (size_t i = 1; i < allNotes.size(); ++i) {
					// 如果起始时间相同 (1ms 差值内)，添加到当前和弦
					if (std::abs(allNotes[i].startTime - currentChord.startTime) < 1.0) {
						currentChord.notes.push_back(allNotes[i]);
					}
					else {
						// 否则保存当前和弦，开始新和弦
						channel.push_back(currentChord);
						currentChord = Chord();
						currentChord.startTime = allNotes[i].startTime;
						currentChord.notes.push_back(allNotes[i]);
					}
				}
				// 添加最后一个和弦
				channel.push_back(currentChord);
			}
		}
	}

	// 实际应用中这里会根据真实 Midi 数据进行分组
	void groupNotesIntoChords() {
		std::cout << "正在重新整理和弦..." << std::endl;

		// 为每个通道重新整理音符
		for (auto& channel : channels) {
			// 临时存储所有音符
			std::vector<Note> allNotes;
			for (const auto& chord : channel) {
				for (const auto& note : chord.notes) {
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

			// 将相同起始时间的音符合并为和弦
			if (!allNotes.empty()) {
				Chord currentChord;
				currentChord.startTime = allNotes[0].startTime;
				currentChord.notes.push_back(allNotes[0]);

				for (size_t i = 1; i < allNotes.size(); ++i) {
					// 如果起始时间相同 (1ms 差值内)，添加到当前和弦
					if (std::abs(allNotes[i].startTime - currentChord.startTime) < 1.0) {
						currentChord.notes.push_back(allNotes[i]);
					}
					else {
						// 否则保存当前和弦，开始新和弦
						channel.push_back(currentChord);
						currentChord = Chord();
						currentChord.startTime = allNotes[i].startTime;
						currentChord.notes.push_back(allNotes[i]);
					}
				}
				// 添加最后一个和弦
				channel.push_back(currentChord);
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
		// 钢琴类乐器：0-7 (Acoustic Grand Piano 到 Clavi)
		// 以及一些其他键盘乐器：8-15 (Celesta 到 Dulcimer)
		return (programNumber >= 0 && programNumber <= 15);
	}

	// 判断轨道中的通道是否有效
	bool isTrackChannelValid(const std::vector<Chord>& channelData) {
		if (channelData.empty()) return false;
		int noteCount = 0;
		for (const auto& chord : channelData) {
			noteCount += chord.notes.size();
		}
		return (channelData.size() > 0 || noteCount > 0);
	}

	// 计算单个通道在指定数据中的难度
	double calculateChannelDifficultyFromData(const std::vector<Chord>& channelChords) {
		// 检查通道是否有足够的数据进行计算
		if (channelChords.size() <= 1) {
			return 0;  // 需要至少 2 个和弦
		}

		double totalDifficulty = 0; // 总难度
		int validTransitions = 0; // 记录有效的转换数

		// 从第二个和弦开始计算
		for (size_t i = 1; i < channelChords.size(); ++i) {
			double timeInterval = channelChords[i].startTime - channelChords[i - 1].startTime;

			// 只有时间间隔为正，才计算难度
			if (timeInterval > 0) {
				// 计算各项难度
				double basicDifficulty = calculateBasicDifficulty(timeInterval);
				double noteCountDifficulty = calculateNoteCountDifficulty(channelChords[i].notes.size());
				double spanDifficulty = calculateSpanDifficulty(channelChords[i].notes);
				double chordDifficulty = 0.0;
				if (spanDifficulty > 0) {chordDifficulty = (basicDifficulty + noteCountDifficulty + spanDifficulty) / 3.0;}
				else {chordDifficulty = (basicDifficulty + noteCountDifficulty) / 2.0;}
				totalDifficulty += chordDifficulty;
				validTransitions++;
			}
		}

		// 如果没有有效的转换，返回 0
		if (validTransitions == 0) {
			return 0;
		}

		// 返回通道的平均难度 (需要根据原始算法进行缩放)
		return (totalDifficulty / validTransitions) / 6.0;
	}

	// 计算基础难度 (基于时间间隔)
	double calculateBasicDifficulty(double timeIntervalMs) {
		// 公式: 6000 / 时间间隔毫秒数
		// 100ms 间隔对应“十级难度” (60)，所以用 6000 (6 倍放大)
		if (timeIntervalMs <= 0) return 0;
		return 6000.0 / timeIntervalMs;
	}

	// 计算音符数量难度
	double calculateNoteCountDifficulty(int noteCount) {
		// 1 个音符难度为 2，5 个音符难度为 10
		// 放大 6 倍后：1 个音符难度为 12，5 个音符难度为 60
		return noteCount * 12;
	}

	// 计算跨度难度
	double calculateSpanDifficulty(const std::vector<Note>& notes) {
		// 1 个半音的跨度难度为 5/6，12 个半音 (一个八度) 的跨度难度为 10
		// 放大 6 倍后：1 个半音的跨度难度为 5，12 个半音 (一个八度) 的跨度难度为 60

		// 如果没有音符或只有一个音符，跨度难度为 0
		if (notes.empty() || notes.size() == 1) return 0;

		// 找到最高和最低音符
		int minNote = notes[0].noteNumber;
		int maxNote = notes[0].noteNumber;

		for (const auto& note : notes) {
			minNote = std::min(minNote, note.noteNumber);
			maxNote = std::max(maxNote, note.noteNumber);
		}

		int span = maxNote - minNote;
		// 跨度难度 = 音程跨度 * 5 (6 倍放大系数)
		return span * 5;
	}


	// 计算单个通道的难度
	double calculateChannelDifficulty(int channelIndex);

	// 计算单个轨道的难度
	double calculateTrackDifficulty(const std::vector<Chord>& trackChannel) {
		// 检查轨道是否有足够的数据进行计算
		if (trackChannel.size() <= 1) {
			return 0;  // 需要至少 2 个和弦
		}

		double totalDifficulty = 0; // 总难度
		int validTransitions = 0; // 记录有效的转换数

		// 从第二个和弦开始计算
		for (size_t i = 1; i < trackChannel.size(); ++i) {
			double timeInterval = trackChannel[i].startTime - trackChannel[i - 1].startTime;

			// 只有时间间隔为正，才计算难度
			if (timeInterval > 0) {
				// 计算各项难度
				double basicDifficulty = calculateBasicDifficulty(timeInterval);
				double noteCountDifficulty = calculateNoteCountDifficulty(trackChannel[i].notes.size());
				double spanDifficulty = calculateSpanDifficulty(trackChannel[i].notes);
				double chordDifficulty = 0.0;
				if (spanDifficulty > 0) {chordDifficulty = (basicDifficulty + noteCountDifficulty + spanDifficulty) / 3.0;}
				else {chordDifficulty = (basicDifficulty + noteCountDifficulty) / 2.0;}
				totalDifficulty += chordDifficulty;
				validTransitions++;
			}
		}

		// 如果没有有效的转换，返回 0
		if (validTransitions == 0) {
			return 0;
		}

		// 返回轨道的平均难度 (需要根据原始算法进行缩放)
		return (totalDifficulty / validTransitions) / 6.0;
	}

	// 计算整体难度 (按乐器和通道显示，支持钢琴多音轨)
	double calculateOverallDifficulty() {
		if (allTracks.empty()) return 0;

		std::cout << "\n各通道难度计算:" << std::endl;
		std::cout << "========================" << std::endl;

		// 首先确定每个通道的最终乐器
		std::vector<int> channelFinalInstruments(16, 0);
		for (const auto& track : allTracks) {
		 for (int channel = 0; channel < 16; ++channel) {
			 // 使用最后一个非零的乐器设置，或者如果一直是 0 但有数据则保持 0
			 if (track.channelInstruments[channel] != 0 || isTrackChannelValid(track.channels[channel])) {
				 channelFinalInstruments[channel] = track.channelInstruments[channel];
			 }
		 }
	 }

	 double totalDifficulty = 0;
	 int validChannelCount = 0;

	 // 为每个通道处理数据
	 for (int channel = 0; channel < 16; ++channel) {
		 // 收集该通道在所有轨道中的有效数据
		 std::vector<std::vector<Chord>> validTrackChannels;
		 std::vector<int> validTrackIndices;
		 
		 for (size_t trackIdx = 0; trackIdx < allTracks.size(); ++trackIdx) {
			 if (isTrackChannelValid(allTracks[trackIdx].channels[channel])) {
				 validTrackChannels.push_back(allTracks[trackIdx].channels[channel]);
				 validTrackIndices.push_back(static_cast<int>(trackIdx));
			 }
		 }

		 // 如果该通道没有任何有效数据，跳过
		 if (validTrackChannels.empty()) {
			 continue;
		 }

		 bool isPiano = isPianoInstrument(channelFinalInstruments[channel]);
		 
		 if (isPiano && validTrackChannels.size() > 1) {
			 // 钢琴类乐器且有多条有效音轨
			 std::cout << "通道 " << channel + 1 << "：" << channelFinalInstruments[channel] + 1 
				 << ". " << getInstrumentName(channelFinalInstruments[channel]) << std::endl;
			 
			 std::vector<double> trackDifficulties;
			 for (size_t i = 0; i < validTrackChannels.size(); ++i) {
					// 计算当前轨道的难度
					double difficulty = calculateTrackDifficulty(validTrackChannels[i]);
					trackDifficulties.push_back(difficulty);

					// 统计音符数和和弦数用于显示
					int chordCount = static_cast<int>(validTrackChannels[i].size());
					int noteCount = 0;
					for (const auto& chord : validTrackChannels[i]) {
						noteCount += chord.notes.size();
					}
						
					std::cout << "  “轨道 " << validTrackIndices[i] + 1 << "”难度：" << std::fixed << std::setprecision(2)
						<< difficulty << " (和弦数：" << chordCount
						<< ", 音符数：" << noteCount << ")" << std::endl;
			 }
			 
			 if (!trackDifficulties.empty()) {
				 // 计算平均难度
				 double totalTrackDifficulty = 0;
				 for (double d : trackDifficulties) {
					 totalTrackDifficulty += d;
				 }
				 double avgDifficulty = totalTrackDifficulty / trackDifficulties.size();
				 std::cout << "“通道 " << channel + 1 << "”的平均难度：" << std::fixed << std::setprecision(2)
					 << avgDifficulty << std::endl;
				 totalDifficulty += avgDifficulty;
				 validChannelCount++;
			 } else {
				 // 没有有效的难度计算，但仍计入有效通道
				 totalDifficulty += 0;
				 validChannelCount++;
			 }
		 }
		 else {
			 // 非钢琴或单音轨情况，合并所有轨道的数据
			 std::vector<Chord> mergedChannel;
			 for (const auto& trackChannel : validTrackChannels) {
				 mergedChannel.insert(mergedChannel.end(), trackChannel.begin(), trackChannel.end());
			 }
			 
			 // 按时间排序
			 std::sort(mergedChannel.begin(), mergedChannel.end(),
				 [](const Chord& a, const Chord& b) {
					 return a.startTime < b.startTime;
				 });
			 
			 double channelDifficulty = calculateChannelDifficultyFromData(mergedChannel);
			 std::cout << "通道 " << channel + 1 << "：" << channelFinalInstruments[channel] + 1 
				 << ". " << getInstrumentName(channelFinalInstruments[channel]) 
				 << "，难度：" << std::fixed << std::setprecision(2) << channelDifficulty;
			 
			 if (validTrackChannels.size() > 1) {
				 std::cout << " (多轨道合并)";
			 }
			 std::cout << std::endl;
			 
			 totalDifficulty += channelDifficulty;
			 validChannelCount++;
		 }
	 }

	 // 如果没有有效通道，返回 0
	 if (validChannelCount == 0) {
		 std::cout << "没有有效通道，总难度: 0" << std::endl;
		 return 0;
	 }

	 // 计算总体难度 (平均所有有效通道)
	 double overallDifficulty = totalDifficulty / validChannelCount;
	 std::cout << "\n有效通道数: " << validChannelCount << ", 平均难度: " << std::fixed << std::setprecision(2) << overallDifficulty << std::endl;

	 return overallDifficulty;
	}
};

int main() {
	std::cout << "Midi 难度计算" << std::endl;
	std::cout << "=============" << std::endl;

	// 创建计算器实例
	MidiDifficultyCalculator calculator;

	// 打开文件选择对话框
	std::string midiFilePath = calculator.openFileSelector();

	if (midiFilePath.empty()) {
		std::cout << "未选择 Midi 文件或选择失败。" << std::endl;
		system("pause");
		return 1;
	}

	std::cout << "选择的 Midi 文件: " << midiFilePath << std::endl;

	// 解析 Midi 文件
	if (!calculator.parseMidiFile(midiFilePath)) {
		std::cerr << "解析 Midi 文件失败！" << std::endl;
		system("pause");
		return 1;
	}

	// 计算并显示结果
	double overallDifficulty = calculator.calculateOverallDifficulty();

	std::cout << "\n最终结果:" << std::endl;
	std::cout << "===========" << std::endl;
	std::cout << "Midi 难度: " << std::fixed << std::setprecision(2)
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