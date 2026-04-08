// #pragma execution_character_set("utf-8")  // 预设编译时，字符集为UTF-8 BOM编码，确保中文字符正确显示
#define NOMINMAX  // 禁用Windows头文件中的min和max宏定义，避免与标准库的std::min和std::max冲突
#include <iostream>     // 输入输出流
#include <vector>       // 向量容器
#include <algorithm>    // 算法库 (用于排序、最大最小值等)
#include <iomanip>      // IO 流格式控制 (如设置小数位数)
#include <windows.h>    // Windows API
#include <commdlg.h>    // 通用对话框 (文件选择对话框)
#include <string>       // 字符串类

using namespace std;  // 使用标准命名空间

class MidiDifficultyCalculator {  // 定义一个MIDI难度计算器类

	// 音符结构体
	struct Note {
		int noteNumber;    // MIDI音符编号 (0~127)，表示音高
		double startTime;  // 开始时间 (毫秒)
		double endTime;    // 结束时间 (毫秒)
	};

	// 音结构体 (可以是单音或和弦)
	struct Sound {
		vector<Note> notes;  // 音包含多个音符
		double startTime;    // 音开始时间

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
		vector<vector<Sound>> channels;  // 16个通道的音数据
		vector<int> channelInstruments;  // 16个通道的当前乐器
		unsigned int trackIndex;         // 音轨索引
		
		TrackData(unsigned int index) : trackIndex(index) {
			channels.resize(16);
			channelInstruments.assign(16, 0);  // 默认乐器都是钢琴
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
			singleNoteCount = 0;  // 单音数
			chordCount = 0;       // 和弦数
			noteCount = 0;        // 音符数
			for (const auto& sound : channels[channel]) {
				if (sound.isSingleNote()) {singleNoteCount++;}
				else if (sound.isChord()) {chordCount++;}
				noteCount += sound.notes.size();
			}
		}
	};

private:
	int bpm;  // 每分钟节拍数 (速度)
	vector<vector<Sound> > channels;      // 16个通道的音数据 (用于兼容)
	vector<TrackData> allTracks;          // 所有音轨的数据
	vector<int> finalChannelInstruments;  // 最终每个通道的乐器 (基于所有音轨)

public:
	// 构造函数
	MidiDifficultyCalculator() : bpm(120) {
		channels.resize(16);
		finalChannelInstruments.assign(16, 0);
	}

	// 打开文件选择对话框
	string openFileSelector() {
		OPENFILENAMEA ofn;  // 使用ANSI版本的Windows文件对话框结构体
		char szFile[260] = {0};  // 存储文件路径的缓冲区 (使用char类型字符串，用于适配ANSI版本的API)

		ZeroMemory(&ofn, sizeof(ofn));  // 初始化结构体
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = szFile;  // 指定文件路径缓冲区
		ofn.nMaxFile = sizeof(szFile);  // ANSI版本，使用sizeof
		ofn.lpstrFilter = "MIDI文件\0*.mid;*.midi\0所有文件\0*.*\0";  // 文件类型过滤器
		ofn.nFilterIndex = 1;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;  // 对话框标志

		if (GetOpenFileNameA(&ofn)) {  // 使用ANSI版本的API
			return string(szFile);  // 直接返回选中的文件路径
		}
		return "";  // 如果没有选择文件，则返回空字符串
	}

	// 辅助函数：解析可变长度值
	unsigned int readVariableLength(vector<unsigned char>::const_iterator& it) {
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
			// 正数表示 ticks per quarter note
			if (division == 0 || bpm == 0) return 0.0;

			// 时间 (秒) = (ticks / division) * (60.0 / bpm)
			// 时间 (毫秒) = 时间 (秒) * 1000
			double quarters = static_cast<double>(ticks) / division;
			double seconds = quarters * (60.0 / bpm);
			return seconds * 1000.0;
		}
	}

	// 解析音轨事件
	void parseTrackEvents(const vector<unsigned char>& trackData, int division) {
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

			unsigned char eventType = status & 0xF0;  // 高4位是事件类型
			int currentChannel = status & 0x0F;       // 低4位是通道号

			switch (eventType) {
			case 0x80: // Note Off
			case 0x90: { // Note On
				// 检查是否有足够的数据读取note和velocity
				if (distance(it, end) < 2) break;

				unsigned char note = *it++;
				unsigned char velocity = *it++;

				// 处理音符开启/关闭事件，Note On 力度为0等同于 Note Off
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
				if (distance(it, end) < dataBytes) break;
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
						if (distance(it, end) < 3) break;

						unsigned int tempo =
							(static_cast<unsigned int>(it[0]) << 16) |
							(static_cast<unsigned int>(it[1]) << 8) |
							static_cast<unsigned int>(it[2]);

						// 更新BPM (microseconds per quarter note)
						if (tempo > 0) {
							int newBpm = static_cast<int>(60000000.0 / tempo);
							cout << "更新BPM: " << bpm << " -> " << newBpm
								<< " (tempo: " << tempo << "μs/qnote)" << endl;
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
				// 未知事件类型，跳过处理
				cout << "未知事件类型: 0x" << hex << static_cast<int>(eventType) << dec << endl;
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
			(static_cast<unsigned char>(bytes[1]));
	}

	// 解析MIDI文件
	bool parseMidiFile(const string& filename) {
		cout << "正在解析MIDI文件: " << filename << endl;

		// 打开文件
		FILE* file = nullptr;
		fopen_s(&file, filename.c_str(), "rb");
		if (!file) {
			cerr << "无法打开文件: " << filename << endl;
			return false;
		}

		// 读取MIDI文件头
		char header[14];
		if (fread(header, 1, 14, file) != 14) {
			cerr << "读取MIDI文件头失败!" << endl;
			fclose(file);
			return false;
		}

		// 检查"MThd"标识
		if (memcmp(header, "MThd", 4) != 0) {
			cerr << "不是有效的MIDI文件!" << endl;
			fclose(file);
			return false;
		}

		// 获取格式信息
		unsigned int headerLength = readBigEndian32(header + 4);
		unsigned int format = readBigEndian16(header + 8);
		unsigned int trackCount = readBigEndian16(header + 10);
		int division = static_cast<short>(readBigEndian16(header + 12));

		cout << "文件头长度: " << headerLength << endl;
		cout << "MIDI格式: " << format << endl;
		cout << "轨道数: " << trackCount << endl;
		cout << "时间分割: " << division << endl;

		// 检查division值
		if (division == 0) {
			cerr << "时间分割值不能为 0!" << endl;
			fclose(file);
			return false;
		}

		// 初始化默认BPM
		bpm = 120;
		cout << "初始BPM: " << bpm << endl;

		// 清空并初始化数据
		channels.clear();
		channels.resize(16);
		allTracks.clear();
		finalChannelInstruments.assign(16, 0);

		// 遍历解析每个音轨
		for (unsigned int trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
			char trackHeader[8];
			if (fread(trackHeader, 1, 8, file) != 8) {
				cerr << "读取音轨头失败!" << endl;
				fclose(file);
				return false;
			}

			// 读取音轨长度 (4字节)
			unsigned int trackLength = readBigEndian32(trackHeader + 4);

			// 检查"MTrk"标识
			if (memcmp(trackHeader, "MTrk", 4) != 0) {
				cout << "警告: “音轨 " << trackIndex + 1 << "”不是标准MTrk音轨，跳过..." << endl;
				// 跳过这个音轨的数据
				fseek(file, trackLength, SEEK_CUR);
				continue;
			}

			cout << "“音轨 " << trackIndex + 1 << "”长度: " << trackLength << " 字节" << endl;

			// 检查音轨长度
			if (trackLength == 0) {
				cout << "警告: “音轨 " << trackIndex + 1 << "”长度为0，跳过..." << endl;
				continue;
			}

			if (trackLength > 50000000) {
				cerr << "音轨长度过大: " << trackLength << "，可能文件已损坏" << endl;
				fclose(file);
				return false;
			}

			// 读取音轨数据
			vector<unsigned char> trackData;
			try {
				trackData.resize(trackLength);
				size_t bytesRead = fread(trackData.data(), 1, trackLength, file);
				if (bytesRead != trackLength) {
					cerr << "读取音轨数据不完整! 应该读取: " << trackLength
						<< " 实际读取: " << bytesRead << endl;
					trackData.resize(bytesRead);
				}
			}
			catch (const bad_alloc& e) {
				cerr << "内存分配失败，音轨长度: " << trackLength << " 错误: " << e.what() << endl;
				fclose(file);
				return false;
			}
			catch (const exception& e) {
				cerr << "创建音轨数据时发生异常: " << e.what() << endl;
				fclose(file);
				return false;
			}

			// 为当前音轨创建新的通道数据
			TrackData currentTrack(trackIndex);
			currentTrack.channels.resize(16);
			currentTrack.channelInstruments.assign(16, 0); // 初始化所有通道乐器为0 (Acoustic Grand Piano)

			// 解析当前音轨的事件
			parseTrackEventsForTrack(trackData, division, currentTrack);

			// 将当前音轨添加到所有音轨列表中
			allTracks.push_back(currentTrack);

			cout << "解析完“音轨 " << trackIndex + 1 << "”后，当前BPM: " << bpm << endl;
		}

		fclose(file);

		// 统计信息
		int validChannels = 0;
		int totalNotes = 0;
		int totalChords = 0;
		int totalSingleNotes = 0;  // 总单音数

		cout << "\n通道详细信息: " << endl;
		cout << "================" << endl;

		// 统计所有音轨中每个通道的数据
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
				cout << "通道 " << channel + 1 << ": 单音数: " << singleNoteCount 
				<< ", 和弦数: " << chordCount << ", 音数: " << (singleNoteCount + chordCount) 
				<< ", 音符数: " << noteCount << endl;
				validChannels++;
			}
		}

		cout << "\n统计结果: " << endl;
		cout << "===========" << endl;
		cout << "总通道数: 16" << endl;
		cout << "有效通道数 (包含音符): " << validChannels << endl;
		cout << "音轨数: " << allTracks.size() << endl;
		cout << "总单音数: " << totalSingleNotes << endl;
		cout << "总和弦数: " << totalChords << endl;
		cout << "总音数: " << (totalSingleNotes + totalChords) << endl;
		cout << "总音符数: " << totalNotes << endl;
		cout << "最终BPM: " << bpm << endl;

		return true;
	}

	// 解析单个音轨事件 (用于新音轨数据结构)
	void parseTrackEventsForTrack(const vector<unsigned char>& trackData, int division, TrackData& track) {
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

			unsigned char eventType = status & 0xF0;  // 高4位是事件类型
			int currentChannel = status & 0x0F;       // 低4位是通道号

			switch (eventType) {
			case 0x80: // Note Off
			case 0x90: { // Note On
				// 检查是否有足够的数据读取note和velocity
				if (distance(it, end) < 2) break;

				unsigned char note = *it++;
				unsigned char velocity = *it++;

				// 处理音符开启/关闭事件，Note On力度为0等同于Note Off
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
				if (distance(it, end) < 2) break;
				it += 2;
				break;
			case 0xC0: // Program Change (程序改变) - 乐器切换
				{
					// Program Change 只有1个数据字节
					if (distance(it, end) < 1) break;
					unsigned char programNumber = *it++;
					
					// 更新音轨中通道的乐器
					if (currentChannel < (int)track.channelInstruments.size()) {
						track.channelInstruments[currentChannel] = programNumber;
						cout << "音轨 " << track.trackIndex + 1 << "，通道 " << currentChannel + 1 
							<< "，乐器变更为: " << static_cast<int>(programNumber) + 1 << ". " 
							<< getInstrumentName(programNumber) << endl;
					}
					break;
				}
			case 0xD0: // Channel Pressure (通道压力)
				// 跳过1个数据字节
				if (distance(it, end) < 1) break;
				++it; break;
			case 0xE0: { // Pitch Bend (弯音)
				// 跳过2个数据字节
				if (distance(it, end) < 2) break;
				it += 2; break;
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
						if (distance(it, end) < 3) break;

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
			default:break;}  // 未知事件类型，跳过处理
		}
	}

	// 将轨道中的音符按起始时间分组为音
	void groupTrackNotesIntoSounds(TrackData& track) {
		// 为每个通道重新整理音符
		for (auto& channel : track.channels) {
			// 临时存储所有音符
			vector<Note> allNotes;
			for (const auto& sound : channel) {
				for (const auto& note : sound.notes) {
					allNotes.push_back(note);
				}
			}

			// 清空当前通道
			channel.clear();

			// 按起始时间排序
			sort(allNotes.begin(), allNotes.end(),
				[](const Note& a, const Note& b) {
				return a.startTime < b.startTime;
			});

			// 将相同起始时间的音符合并为音
			if (!allNotes.empty()) {
				Sound currentSound;  // 当前音
				currentSound.startTime = allNotes[0].startTime;
				currentSound.notes.push_back(allNotes[0]);

				for (size_t i = 1; i < allNotes.size(); ++i) {
					// 如果起始时间相同 (1ms差值内)，添加到当前音
					if (abs(allNotes[i].startTime - currentSound.startTime) < 1.0) {
						currentSound.notes.push_back(allNotes[i]);
					}
					else {
						// 否则保存当前音，开始新音
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
		cout << "正在重新整理音..." << endl;

		// 为每个通道重新整理音符
		for (auto& channel : channels) {
			// 临时存储所有音符
			vector<Note> allNotes;
			for (const auto& sound : channel) {
				for (const auto& note : sound.notes) {
					allNotes.push_back(note);
				}
			}

			// 清空当前通道
			channel.clear();

			// 按起始时间排序
			sort(allNotes.begin(), allNotes.end(),
				[](const Note& a, const Note& b) {
				return a.startTime < b.startTime;
			});

			// 将相同起始时间的音符合并为音
			if (!allNotes.empty()) {
				Sound currentSound;
				currentSound.startTime = allNotes[0].startTime;
				currentSound.notes.push_back(allNotes[0]);

				for (size_t i = 1; i < allNotes.size(); ++i) {
					// 如果起始时间相同 (1ms差值内)，添加到当前音
					if (abs(allNotes[i].startTime - currentSound.startTime) < 1.0) {
						currentSound.notes.push_back(allNotes[i]);
					}
					else {
						// 否则保存当前音，开始新音
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
	string getInstrumentName(int programNumber) {
		static const vector<string> gmInstruments = {
			// 1~8: 钢琴
			"Acoustic Grand Piano [声学大钢琴/原声大钢琴/平台钢琴]",
			"Bright Acoustic Piano [亮(音)钢琴]",
			"Electric Grand Piano [电子大钢琴/平台电钢琴]",
			"Honky-tonk Piano [酒吧钢琴/走音钢琴]",
			"Electric Piano 1 [电钢琴 1]",
			"Electric Piano 2 [电钢琴 2]",
			"Harpsichord [大键琴/羽管键琴/拨弦古钢琴]",
			"Clavi [古钢琴/(电子)击弦古钢琴]",

			// 9~16: 半音打击乐器
			"Celesta [钢片琴]",
			"Glockenspiel [钟琴]",
			"Music Box [八音盒/音乐盒]",
			"Vibraphone [颤音琴/抖音琴/震动竖琴]",
			"Marimba [立奏木琴/马林巴琴]",
			"Xylophone [(柔音)木琴]",
			"Tubular Bells [管钟(琴)]",
			"Dulcimer [扬琴/德西马琴]",

			// 17~24: 风琴
			"Drawbar Organ [电风琴/拉杆风琴/哈蒙德风琴]",
			"Percussive Organ [敲击风琴/击音管风琴]",
			"Rock Organ [摇滚风琴]",
			"Church Organ [教会风琴/教堂(管)风琴]",
			"Reed Organ [簧片风琴]",
			"Accordion [手风琴]",
			"Harmonica [口琴]",
			"Tango Accordion [探戈手风琴]",

			// 25~32: 吉他
			"Acoustic Guitar (nylon) [古典吉他/尼龙弦吉他]",
			"Acoustic Guitar (steel) [民谣吉他/钢弦吉他]",
			"Electric Guitar (jazz) [爵士(电)吉他]",
			"Electric Guitar (clean) [原音(电)吉他/纯音吉他]",
			"Electric Guitar (muted) [弱音(电)吉他/闷音吉他]",
			"Overdriven Guitar [破音(电)吉他/激励音吉他]",
			"Distortion Guitar [噪音(电)吉他/强烈的破音(电)吉他/失真吉他]",
			"Guitar harmonics [泛音(电)吉他]",

			// 33~40: 贝司
			"Acoustic Bass [原声贝司/低音大提琴]",
			"Electric Bass (finger) [指弹电贝司/手弹电贝司]",
			"Electric Bass (pick) [拨片电贝司/弹片电贝司]",
			"Fretless Bass [无品贝司/无格贝司]",
			"Slap Bass 1 [打弦贝司 1/甩指贝司 1 (拇指敲击弹奏)]",
			"Slap Bass 2 [打弦贝司 2/甩指贝司 2 (拇指敲击弹奏)]",
			"Synth Bass 1 [合成贝司 1]", "Synth Bass 2 [合成贝司 2]",

			// 41~48: 弦乐器
			"Violin [小提琴]",
			"Viola [中提琴]",
			"Cello [大提琴]",
			"Contrabass [低音提琴]",
			"Tremolo Strings [颤音弦乐/震音弦乐]",
			"Pizzicato Strings [拨奏弦乐/弹拨弦乐]",
			"Orchestral Harp [竖琴]",
			"Timpani [定音鼓/定音弦乐器声]",

			// 49~56: 合奏组/合唱组
			"String Ensemble 1 [弦乐合奏 1]",
			"String Ensemble 2 [弦乐合奏 2]",
			"SynthStrings 1 [合成弦乐 1]",
			"SynthStrings 2 [合成弦乐 2]",
			"Choir Aahs [合唱“啊”声/教堂唱诗班“啊”声]",
			"Voice Oohs [合唱“哦”声/教堂唱诗班“哦”声]",
			"Synth Voice [合成人声]",
			"Orchestra Hit [管弦乐齐奏/打击交响乐]",

			// 57~64: 铜管乐器
			"Trumpet [小号]",
			"Trombone [长号]",
			"Tuba [大号/低音号]",
			"Muted Trumpet [弱音喇叭/闷音小喇叭/小号＋弱音器]",
			"French Horn [法国号]",
			"Brass Section [铜管乐合奏]",
			"SynthBrass 1 [合成铜管乐 1/合成钟管 1]",
			"SynthBrass 2 [合成铜管乐 2/合成钟管 2]",

			// 65~72: 簧片乐器
			"Soprano Sax [高音萨克斯]",
			"Alto Sax [中音萨克斯]",
			"Tenor Sax [次中音萨克斯]",
			"Baritone Sax [(上)低音萨克斯]",
			"Oboe [双簧管]",
			"English Horn [英国管/英国小号声]",
			"Bassoon [巴松管/大管]",
			"Clarinet [单簧管/黑管]",

			// 73~80: 管鸣乐器
			"Piccolo [短笛]",
			"Flute [长笛]",
			"Recorder [竖笛/木笛/直笛]",
			"Pan Flute [排笛]",
			"Blown Bottle [(吹)瓶笛/吹瓶子声]",
			"Shakuhachi [尺八竹笛 (日本乐器)]",
			"Whistle [汽笛声/口哨]",
			"Ocarina [陶笛]",

			// 81~88: 合成主音
			"Lead 1 (square) [合成主音 1 (方波/长方形卧式钢琴声)]",
			"Lead 2 (sawtooth) [合成主音 2 (锯齿波/拉锯声)]",
			"Lead 3 (calliope) [合成主音 3 ((蒸)汽笛风琴声)]",
			"Lead 4 (chiff) [合成主音 4 (吹管声/棕榴莺声)]",
			"Lead 5 (charang) [合成主音 5 (吉他/卡那声)]",
			"Lead 6 (voice) [合成主音 6 (人声/说话声)]",
			"Lead 7 (fifths) [合成主音 7 ((平行)五度和声)]",
			"Lead 8 (bass + lead) [合成主音 8 (贝司＋主音/低音和主旋律)]",

			// 89~96: 合成柔音
			"Pad 1 (new age) [合成柔音 1 (新世纪声/新时代声)]",
			"Pad 2 (warm) [合成柔音 2 (暖音/温暖的/热情声)]",
			"Pad 3 (polysynth) [合成柔音 3 (复合合成音/多种合成音)]",
			"Pad 4 (choir) [合成柔音 4 (合唱声/唱诗班声)]",
			"Pad 5 (bowed) [合成柔音 5 (弓弦音色/低音琴弓声)]",
			"Pad 6 (metallic) [合成柔音 6 (金属声)]",
			"Pad 7 (halo) [合成柔音 7 (光环声/问候/气氛包围声)]",
			"Pad 8 (sweep) [合成柔音 8 (风吹声/扫弦/宽阔的)]",

			// 97~104: 合成效果
			"FX 1 (rain) [合成效果 1 (雨声)]",
			"FX 2 (soundtrack) [合成效果 2 (音轨/电音配音乐)]",
			"FX 3 (crystal) [合成效果 3 (水晶/晶体/清澈的水晶声)]",
			"FX 4 (atmosphere) [合成效果 4 (大气/气氛/自然气氛声)]",
			"FX 5 (brightness) [合成效果 5 (明亮/晴朗天气声)]",
			"FX 6 (goblins) [合成效果 6 (精灵/鬼怪/坏天气声)]",
			"FX 7 (echoes) [合成效果 7 ((空谷)回声)]",
			"FX 8 (sci-fi) [合成效果 8 (科幻/科学幻想声)]",

			// 105~112: 民间乐器
			"Sitar [西塔尔琴]",
			"Banjo [斑卓(琴)/班鸠琴]",
			"Shamisen [三弦琴/三味线/撒米森琴]",
			"Koto [古筝/日本筝/日本十三弦琴]",
			"Kalimba [卡林巴琴/克林巴琴]",
			"Bag pipe [(苏格兰)风笛]",
			"Fiddle [古提琴/爱尔兰小提琴/提琴类乐器声]",
			"Shanai [唢呐/山奈琴]",

			// 113~120: 打击乐器
			"Tinkle Bell [铃铛声/叮当铃声]",
			"Agogo [拉丁打铃/阿果果鼓声/阿戈戈鼓声]",
			"Steel Drums [钢鼓/钢板鼓声]",
			"Woodblock [木鱼/木板鼓声]",
			"Taiko Drum [太鼓/秦可鼓声]",
			"Melodic Tom [嗵鼓/旋律中音鼓(声)]",
			"Synth Drum [合成鼓(声)]",
			"Reverse Cymbal [反钹/回音钹/饶声]",

			// 121~128: 音响效果 (通道10专用)
			// 通道10的乐器是根据音符编号来确定的，不同于其他通道的Program Change
			"Guitar Fret Noise [吉他擦弦杂音/摩擦噪声]",
			"Breath Noise [呼吸声/吹奏气音/吹瓶子声]",
			"Seashore [海浪声]",
			"Bird Tweet [鸟鸣声/鸟叫声]",
			"Telephone Ring [电话铃(声)]",
			"Helicopter [直升飞机声]",
			"Applause [鼓掌声]",
			"Gunshot [枪声/射击声]"
		};
		
		if (programNumber >= 0 && programNumber < static_cast<int>(gmInstruments.size())) {
			return gmInstruments[programNumber];
		}
		return "Unknown Instrument";
	}

	// 判断是否为钢琴类乐器 (键盘乐器)
	bool isPianoInstrument(int programNumber) {
		// 钢琴类乐器：0~7 (Acoustic Grand Piano到Clavi)
		// 以及一些其他键盘乐器：8~15 (Celesta到Dulcimer)
		return (programNumber >= 0 && programNumber <= 15);
	}

	// 判断音轨中的通道是否有效
	bool isTrackChannelValid(const vector<Sound>& channelData) {
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
			// 第一个音的“基础难度”已经设为0，所以不需要考虑basicDifficulty
			if (sound.isSingleNote()) {
				// 第一个音为“单音”时：只有“音数难度”
				return noteCountDifficulty;
			}
			else if (sound.isChord()) {
				// 第一个音为“和弦”时：取“音数难度”和“跨度难度”的平均值
				if (spanDifficulty > 0) {
					// 有跨度的“和弦”：取两项平均值
					return (noteCountDifficulty + spanDifficulty) / 2.0;
				} else {
					// 无跨度的“和弦”：只有“音数难度”，跟“单音”一样
					return noteCountDifficulty;  // 理论上不会发生
				}
			}
		}
		// 如果不是第一个音 (后续音)
		else {
			// 单音 (单个音符)
			if (sound.isSingleNote()) {
				// 单音没有“跨度难度”，所以取“基础难度”和“音数难度”的平均值
				return (basicDifficulty + noteCountDifficulty) / 2.0;
			}
			// 和弦 (多个音符)
			else if (sound.isChord()) {
				if (spanDifficulty > 0) {
					// 有跨度的“和弦”：取三项平均值
					return (basicDifficulty + noteCountDifficulty + spanDifficulty) / 3.0;
				} else {
					// 无跨度的“和弦”：取两项平均值，跟“单音”一样
					return (basicDifficulty + noteCountDifficulty) / 2.0;  // 理论上不会发生
				}
			}
		}
		return 0;
	}

	// 计算单个通道在指定数据中的难度
	double calculateChannelDifficulty(const vector<Sound>& channelSounds) {
		// 检查通道是否有数据
		if (channelSounds.empty()) {return 0;}

		double totalDifficulty = 0; // 总难度

		// 处理第一个音 (首音)
		if (!channelSounds.empty()) {
			// 第一个音的基础难度为0，并标记为第一个音
			double firstSoundDifficulty = calculateSoundDifficulty(channelSounds[0], 0.0, true);
			totalDifficulty += firstSoundDifficulty;
		}

		// 从第二个音开始计算 (后续音)
		for (size_t i = 1; i < channelSounds.size(); ++i) {
			double timeInterval = channelSounds[i].startTime - channelSounds[i - 1].startTime;

			// 只有时间间隔为正，才计算“基础难度”
			double basicDifficulty = 0.0;
			if (timeInterval > 0) {basicDifficulty = calculateBasicDifficulty(timeInterval);}
			
			// 不是第一个音，所以isFirstSound为false
			double soundDifficulty = calculateSoundDifficulty(channelSounds[i], basicDifficulty, false);
			totalDifficulty += soundDifficulty;
		}

		// 返回通道的平均难度 (总难度除以“音”的总数，再除以6，还原为“真实”难度值)
		return totalDifficulty / static_cast<double>(channelSounds.size()) / 6.0;
	}

	// 计算“基础难度” (基于“时间间隔”)
	double calculateBasicDifficulty(double timeIntervalMs) {
		// 公式：6000 / 时间间隔 (ms)
		// 考虑到人的极限反应时间约为0.1s，遂将100ms对应“十级难度”，即用6000来除 (6倍放大)
		if (timeIntervalMs <= 0) return 0;
		return 6000.0 / timeIntervalMs;  // 时间间隔越短 (演奏速度越快)，难度越大
	}

	// 计算单个音的难度 (仅基础)
	double calculateBasicOnlySoundDifficulty(const Sound& sound, double basicDifficulty, bool isFirstSound = false) {
		if (sound.notes.empty()) return 0;
		
		// 如果是第一个音 (首音)
		if (isFirstSound) {
			// 第一个音的“基础难度”已经设为0
			return 0;
		}
		// 如果不是第一个音 (后续音)
		else {
			// 只考虑基础难度 (注意：这里的“基础难度”已经是6倍放大的值)
			return basicDifficulty;
		}
		return 0;
	}

	// 计算单个通道在指定数据中的难度 (仅基础)
	double calculateBasicOnlyChannelDifficulty(const vector<Sound>& channelSounds) {
		// 检查通道是否有数据
		if (channelSounds.empty()) {return 0;}

		double totalDifficulty = 0; // 总难度

		// 处理第一个音 (首音)
		if (!channelSounds.empty()) {
			// 将第一个音的“基础难度”设为0，并标记为第一个音
			double firstSoundDifficulty = calculateBasicOnlySoundDifficulty(channelSounds[0], 0.0, true);
			totalDifficulty += firstSoundDifficulty;
		}

		// 从第二个音开始计算 (后续音)
		for (size_t i = 1; i < channelSounds.size(); ++i) {
			double timeInterval = channelSounds[i].startTime - channelSounds[i - 1].startTime;

			// 只有时间间隔为正，才计算“基础难度”
			double basicDifficulty = 0.0;
			if (timeInterval > 0) {basicDifficulty = calculateBasicDifficulty(timeInterval);}
			
			// 不是第一个音，所以isFirstSound为false
			double soundDifficulty = calculateBasicOnlySoundDifficulty(channelSounds[i], basicDifficulty, false);
			totalDifficulty += soundDifficulty;
		}

		// 返回通道的平均难度 (总难度除以“音”的总数，再除以6，还原为“真实”难度值)
		// 注意：由于calculateBasicDifficulty返回的是6倍放大的值，所以需要除以6还原
		return totalDifficulty / static_cast<double>(channelSounds.size()) / 6.0;
	}

	// 计算单个音轨的难度 (仅基础)
	double calculateBasicOnlyTrackDifficulty(const vector<Sound>& trackChannel) {
		// 直接调用通道难度计算函数
		return calculateBasicOnlyChannelDifficulty(trackChannel);
	}

	// 计算整体难度 (仅基础) (按乐器和通道显示，支持钢琴多音轨)
	double calculateBasicOnlyOverallDifficulty(double& midiSumDifficulty) {
		if (allTracks.empty()) {
			midiSumDifficulty = 0;
			return 0;
		}

		cout << "\n各通道难度计算 (仅基础):" << endl;
		cout << "========================" << endl;

		// 收集所有有效的通道数据
		vector<vector<Sound>> allValidChannels;
		for (const auto& track : allTracks) {
			for (int channel = 0; channel < 16; ++channel) {
				if (track.isChannelValid(channel)) {
					allValidChannels.push_back(track.channels[channel]);
				}
			}
		}

		if (allValidChannels.empty()) {
			cout << "没有有效的音轨数据。" << endl;
			midiSumDifficulty = 0;
			return 0;
		}

		// 如果只有一个有效通道，直接计算其难度
		if (allValidChannels.size() == 1) {
			 double channelDifficulty = calculateBasicOnlyChannelDifficulty(allValidChannels[0]);
			 cout << "单一通道平均难度 (仅基础): " << channelDifficulty << endl;
			 cout << "单一通道总难度 (仅基础): " << channelDifficulty << endl;
			 midiSumDifficulty = channelDifficulty;
			 return channelDifficulty;
		}

		// 确定每个通道的最终乐器
		vector<int> channelFinalInstruments(16, 0); // 默认都是钢琴(0)
		for (int channel = 0; channel < 16; ++channel) {
			// 从最后一个音轨开始查找，直到找到第一个设置了乐器的音轨为止
			for (int trackIdx = static_cast<int>(allTracks.size()) - 1; trackIdx >= 0; --trackIdx) {
				if (allTracks[trackIdx].channelInstruments[channel] != 0 || 
					!allTracks[trackIdx].channels[channel].empty()) {
					channelFinalInstruments[channel] = allTracks[trackIdx].channelInstruments[channel];
					break;
				}
			}
		}

		double totalAverageDifficulty = 0;  // 总平均难度 (用于计算MIDI平均难度)
		double totalSumDifficulty = 0;      // 总难度 (累加值)
		int validChannelCount = 0;          // 有效通道计数

		// 为每个通道处理数据
		for (int channel = 0; channel < 16; ++channel) {
			// 收集该通道在所有音轨中的有效数据
			vector<vector<Sound>> validTrackChannels;
			vector<int> validTrackIndices;  // 记录对应的音轨索引用于显示
			 
			for (size_t trackIdx = 0; trackIdx < allTracks.size(); ++trackIdx) {
				if (isTrackChannelValid(allTracks[trackIdx].channels[channel])) {
					validTrackChannels.push_back(allTracks[trackIdx].channels[channel]);
					validTrackIndices.push_back(static_cast<int>(trackIdx));
				}
			}

			// 如果该通道没有任何有效数据，则跳过
			if (validTrackChannels.empty()) {continue;}

			bool isPiano = isPianoInstrument(channelFinalInstruments[channel]);

			if (validTrackChannels.size() > 1) {
				// 多条有效音轨
				cout << "通道 " << channel + 1 << ": " << channelFinalInstruments[channel] + 1 
					<< ". " << getInstrumentName(channelFinalInstruments[channel]) << endl;

				double channelAverageDifficulty = 0;  // 通道的平均难度
				double channelTotalDifficulty = 0;    // 通道的总难度 (累加值)

				for (size_t i = 0; i < validTrackChannels.size(); ++i) {
					// 计算当前音轨的难度 (仅基础)
					double difficulty = calculateBasicOnlyTrackDifficulty(validTrackChannels[i]);
					channelTotalDifficulty += difficulty;

					// 统计单音数、和弦数和音符数用于显示 (仅基础)
					int singleNoteCount = 0, chordCount = 0, noteCount = 0;
					for (const auto& sound : validTrackChannels[i]) {
						if (sound.isSingleNote()) singleNoteCount++;
						else if (sound.isChord()) chordCount++;
						noteCount += sound.notes.size();
					}

					cout << "  “音轨 " << validTrackIndices[i] + 1 << "” (仅基础): " << fixed << setprecision(2)
						<< difficulty << " (单音数: " << singleNoteCount << "，和弦数: " << chordCount
						<< "，音数: " << (singleNoteCount + chordCount) << "，音符数: " << noteCount << ")" << endl;
				}

				// 计算所有音轨的平均难度 (仅基础)
				channelAverageDifficulty = channelTotalDifficulty / validTrackChannels.size();
				
				cout << "“通道 " << channel + 1 << "”的平均难度 (仅基础): " << fixed << setprecision(2)
				 << channelAverageDifficulty << endl;
				cout << "“通道 " << channel + 1 << "”的总难度 (仅基础): " << fixed << setprecision(2)
				 << channelTotalDifficulty << endl;

				totalAverageDifficulty += channelAverageDifficulty;
				totalSumDifficulty += channelTotalDifficulty;
				validChannelCount++;  // 增加“有效通道”计数
			} else {
				// 单音轨情况，直接计算该通道的难度 (仅基础)
				double channelDifficulty = calculateBasicOnlyChannelDifficulty(validTrackChannels[0]);
				cout << "通道 " << channel + 1 << ": " << channelFinalInstruments[channel] + 1 
				 << ". " << getInstrumentName(channelFinalInstruments[channel]) 
				 << "，平均难度 (仅基础): " << fixed << setprecision(2) << channelDifficulty;
				cout << "，总难度 (仅基础): " << fixed << setprecision(2) << channelDifficulty << endl;

				totalAverageDifficulty += channelDifficulty;
				totalSumDifficulty += channelDifficulty;
				validChannelCount++;  // 增加“有效通道”计数
			}
		}

		cout << "\n难度最终结果 (仅基础):" << endl;
		cout << "=====================" << endl;

		// 如果没有“有效通道”，则返回0
		if (validChannelCount == 0) {
			cout << "没有有效通道，总难度 (仅基础): 0" << endl;
			midiSumDifficulty = 0;
			return 0;
		}

		// 计算MIDI平均难度和MIDI难度 (均为仅基础)
		double midiAverageDifficulty = totalAverageDifficulty / validChannelCount;
		midiSumDifficulty = totalSumDifficulty;

		cout << "\nMIDI平均难度 (仅基础): " << fixed << setprecision(2) << midiAverageDifficulty << endl;
		cout << "MIDI难度 (仅基础): " << fixed << setprecision(2) << midiSumDifficulty << endl;

		// 返回MIDI平均难度 (仅基础)
		return midiAverageDifficulty;
	}

	// 计算整体难度 (按乐器和通道显示，支持钢琴多音轨)
	double calculateOverallDifficulty(double& midiSumDifficulty) {
		if (allTracks.empty()) {
			midiSumDifficulty = 0;
			return 0;
		}

		cout << "\n各通道难度计算:" << endl;
		cout << "========================" << endl;

		// 收集所有有效的通道数据
		vector<vector<Sound>> allValidChannels;
		for (const auto& track : allTracks) {
			for (int channel = 0; channel < 16; ++channel) {
				if (track.isChannelValid(channel)) {
					allValidChannels.push_back(track.channels[channel]);
				}
			}
		}

		if (allValidChannels.empty()) {
			cout << "没有有效的音轨数据。" << endl;
			midiSumDifficulty = 0;
			return 0;
		}

		// 如果只有一个有效通道，直接计算其难度
		if (allValidChannels.size() == 1) {
			 double channelDifficulty = calculateChannelDifficulty(allValidChannels[0]);
			 cout << "单一通道平均难度: " << channelDifficulty << endl;
			 cout << "单一通道总难度: " << channelDifficulty << endl;
			 midiSumDifficulty = channelDifficulty;
			 return channelDifficulty;
		}

		// 确定每个通道的最终乐器
		vector<int> channelFinalInstruments(16, 0); // 默认都是钢琴(0)
		for (int channel = 0; channel < 16; ++channel) {
			// 从最后一个音轨开始查找，直到找到第一个设置了乐器的音轨为止
			for (int trackIdx = static_cast<int>(allTracks.size()) - 1; trackIdx >= 0; --trackIdx) {
				if (allTracks[trackIdx].channelInstruments[channel] != 0 || 
					!allTracks[trackIdx].channels[channel].empty()) {
					channelFinalInstruments[channel] = allTracks[trackIdx].channelInstruments[channel];
					break;
				}
			}
		}

		double totalAverageDifficulty = 0;  // 总平均难度 (用于计算MIDI平均难度)
		double totalSumDifficulty = 0;      // 总难度 (累加值)
		int validChannelCount = 0;          // 有效通道计数

		// 为每个通道处理数据
		for (int channel = 0; channel < 16; ++channel) {
			// 收集该通道在所有音轨中的有效数据
			vector<vector<Sound>> validTrackChannels;
			vector<int> validTrackIndices;  // 记录对应的音轨索引用于显示
			 
			for (size_t trackIdx = 0; trackIdx < allTracks.size(); ++trackIdx) {
				if (isTrackChannelValid(allTracks[trackIdx].channels[channel])) {
					validTrackChannels.push_back(allTracks[trackIdx].channels[channel]);
					validTrackIndices.push_back(static_cast<int>(trackIdx));
				}
			}

			// 如果该通道没有任何有效数据，则跳过
			if (validTrackChannels.empty()) {continue;}

			bool isPiano = isPianoInstrument(channelFinalInstruments[channel]);

			if (validTrackChannels.size() > 1) {
				// 多条有效音轨
				cout << "通道 " << channel + 1 << ": " << channelFinalInstruments[channel] + 1 
					<< ". " << getInstrumentName(channelFinalInstruments[channel]) << endl;

				double channelAverageDifficulty = 0;  // 通道的平均难度
				double channelTotalDifficulty = 0;    // 通道的总难度 (累加值)
				
				for (size_t i = 0; i < validTrackChannels.size(); ++i) {
					// 计算当前音轨的难度
					double difficulty = calculateTrackDifficulty(validTrackChannels[i]);
					channelTotalDifficulty += difficulty;

					// 统计单音数、和弦数和音符数用于显示
					int singleNoteCount = 0, chordCount = 0, noteCount = 0;
					for (const auto& sound : validTrackChannels[i]) {
						if (sound.isSingleNote()) singleNoteCount++;
						else if (sound.isChord()) chordCount++;
						noteCount += sound.notes.size();
					}

					cout << "  “音轨 " << validTrackIndices[i] + 1 << "”难度: " << fixed << setprecision(2)
						<< difficulty << " (单音数: " << singleNoteCount << "，和弦数: " << chordCount
						<< "，音数: " << (singleNoteCount + chordCount) << "，音符数: " << noteCount << ")" << endl;
				}

				// 计算所有音轨的平均难度
				channelAverageDifficulty = channelTotalDifficulty / validTrackChannels.size();

				cout << "“通道 " << channel + 1 << "”的平均难度: " << fixed << setprecision(2)
				 << channelAverageDifficulty << endl;
				cout << "“通道 " << channel + 1 << "”的总难度: " << fixed << setprecision(2)
				 << channelTotalDifficulty << endl;

				totalAverageDifficulty += channelAverageDifficulty;
				totalSumDifficulty += channelTotalDifficulty;
				validChannelCount++;  // 增加“有效通道”计数
			} else {
				// 单音轨情况，直接计算该通道的难度
				double channelDifficulty = calculateChannelDifficulty(validTrackChannels[0]);
				cout << "通道 " << channel + 1 << ": " << channelFinalInstruments[channel] + 1 
				 << ". " << getInstrumentName(channelFinalInstruments[channel]) 
				 << "，平均难度: " << fixed << setprecision(2) << channelDifficulty;
				cout << "，总难度: " << fixed << setprecision(2) << channelDifficulty << endl;
			 
				totalAverageDifficulty += channelDifficulty;
				totalSumDifficulty += channelDifficulty;
				validChannelCount++;  // 增加“有效通道”计数
			}
		}

		cout << "\n最终结果:" << endl;
		cout << "============" << endl;

		// 如果没有“有效通道”，则返回0
		if (validChannelCount == 0) {
			cout << "没有有效通道，总难度: 0" << endl;
			midiSumDifficulty = 0;
			return 0;
		}

		// 计算MIDI平均难度和MIDI难度
		double midiAverageDifficulty = totalAverageDifficulty / validChannelCount;
		midiSumDifficulty = totalSumDifficulty;

		cout << "\nMIDI平均难度: " << fixed << setprecision(2) << midiAverageDifficulty << endl;
		cout << "MIDI难度: " << fixed << setprecision(2) << midiSumDifficulty << endl;

		// 返回MIDI平均难度
		return midiAverageDifficulty;
	}

	// 计算“音数难度”
	double calculateNoteCountDifficulty(int noteCount) {
		// 1个音符难度为2, 5个音符难度为10 (原始难度)
		// 1个音符难度为12, 5个音符难度为60 (6倍放大)
		return noteCount * 12;
	}

	// 计算“跨度难度”
	double calculateSpanDifficulty(const vector<Note>& notes) {
		// 1个半音难度为5/6, 12个半音 (一个八度) 难度为10
		// 1个半音难度为5, 12个半音 (一个八度) 难度为60 (6倍放大)

		// 如果没有音符或仅由一个音符构成的音 (单音), 跨度难度为0
		if (notes.empty() || notes.size() == 1) return 0;

		// 找出“根音”和“冠音”
		int minNote = notes[0].noteNumber;
		int maxNote = notes[0].noteNumber;

		for (const auto& note : notes) {
			minNote = min(minNote, note.noteNumber);
			maxNote = max(maxNote, note.noteNumber);
		}

		int span = maxNote - minNote;
		// 跨度难度 = 音程跨度 * 5 (6倍放大)
		return span * 5;
	}

	// 计算单个音轨的难度
	double calculateTrackDifficulty(const vector<Sound>& trackChannel) {
		// 直接调用通道难度计算函数
		return calculateChannelDifficulty(trackChannel);
	}
};

int main() {
	cout << "MIDI难度计算" << endl;
	cout << "==============" << endl;

	// 创建计算器实例
	MidiDifficultyCalculator calculator;

	// 打开文件选择对话框
	string midiFilePath = calculator.openFileSelector();

	if (midiFilePath.empty()) {
		cout << "未选择MIDI文件或选择失败。" << endl;
		system("pause");  // 等待用户按键后退出
		return 1;
	}

	cout << "选择的MIDI文件: " << midiFilePath << endl;

	// 解析MIDI文件
	if (!calculator.parseMidiFile(midiFilePath)) {
		cerr << "解析MIDI文件失败!" << endl;
		system("pause");  // 等待用户按键后退出
		return 1;
	}

	// 计算并显示难度结果 (包含三部分)
	cout << "\n=== 难度计算结果 (包含三部分) ===" << endl;
	double midiSumDifficulty;
	double midiAverageDifficulty = calculator.calculateOverallDifficulty(midiSumDifficulty);

	// MIDI平均难度等级判断 (包含三部分)
	cout << "\n平均难度等级 (包含三部分): ";
	if (midiAverageDifficulty >= 8) {
		cout << "专业级 (8~10)" << endl;
	} else if (midiAverageDifficulty >= 6) {
		cout << "高级 (6~8)" << endl;
	} else if (midiAverageDifficulty >= 4) {
		cout << "中级 (4~6)" << endl;
	} else if (midiAverageDifficulty >= 2) {
		cout << "初级 (2~4)" << endl;
	} else {
		cout << "入门级 (0~2)" << endl;
	}

	// 计算并显示难度结果（仅基础）
	cout << "\n=== 难度计算结果 (仅基础) ===" << endl;
	double basicOnlyMidiSumDifficulty;
	double basicOnlyMidiAverageDifficulty = calculator.calculateBasicOnlyOverallDifficulty(basicOnlyMidiSumDifficulty);

	// 难度等级判断 (仅基础)
	cout << "\n平均难度等级 (仅基础): ";
	if (basicOnlyMidiAverageDifficulty >= 8) {
		cout << "专业级 (8~10)" << endl;
	} else if (basicOnlyMidiAverageDifficulty >= 6) {
		cout << "高级 (6~8)" << endl;
	} else if (basicOnlyMidiAverageDifficulty >= 4) {
		cout << "中级 (4~6)" << endl;
	} else if (basicOnlyMidiAverageDifficulty >= 2) {
		cout << "初级 (2~4)" << endl;
	} else {
		cout << "入门级 (0~2)" << endl;
	}

	system("pause");  // 等待用户按键后退出
	return 0;
}