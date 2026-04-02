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
		int noteNumber;     // MIDI 音符编号 (0-127)，表示音高
		double startTime;   // 开始时间 (毫秒)
		double endTime;     // 结束时间 (毫秒)
	};

	// 和弦结构体 (组合音)
	struct Chord {
		std::vector<Note> notes;  // 和弦包含多个音符
		double startTime;         // 和弦开始时间
	};

private:
	int bpm;  // 每分钟节拍数 (速度)
	std::vector<std::vector<Chord> > channels;  // 16 个通道的和弦数据

public:
	// 打开文件选择对话框
	std::string openFileSelector() {
		OPENFILENAMEA ofn;  // 使用ANSI版本的Windows文件对话框结构体
		char szFile[260] = { 0 };  // 存储文件路径的缓冲区（使用char）

		ZeroMemory(&ofn, sizeof(ofn));  // 初始化结构体
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = szFile;  // 指定文件路径缓冲区
		ofn.nMaxFile = sizeof(szFile);  // ANSI版本，直接使用sizeof
		ofn.lpstrFilter = "MIDI文件\0*.mid;*.midi\0所有文件\0*.*\0";  // 文件类型过滤器
		ofn.nFilterIndex = 1;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;  // 对话框标志

		if (GetOpenFileNameA(&ofn)) {  // 使用ANSI版本的API
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
			else {
				lastStatus = status;
			}

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
				if (eventType == 0x90 && velocity == 0) {
					// 简化处理，当作 Note Off
				}
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
						// 为了简化，我们将同时刻的音符记为和弦处理
						if (channels[currentChannel].empty() ||
							channels[currentChannel].back().startTime != timeInMs) {
							Chord newChord;
							newChord.startTime = timeInMs;
							newChord.notes.push_back(newNote);
							channels[currentChannel].push_back(newChord);
						}
						else {
							channels[currentChannel].back().notes.push_back(newNote);
						}
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
				for (int i = 0; i < dataBytes; ++i) {
					++it;
				}
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
					for (unsigned int i = 0; i < length && it != end; ++i) {
						++it;
					}
				}
				else if (status == 0xF0 || status == 0xF7) { // SysEx (系统独占消息)
					if (it == end) break;
					unsigned int length = readVariableLength(it);
					// 跳过 SysEx 数据
					for (unsigned int i = 0; i < length && it != end; ++i) {
						++it;
					}
				}
				else {
					// 其他系统信息
					++it;
				}
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
		unsigned int trackCount = readBigEndian16(header + 10); // 修正拼写错误
		int division = static_cast<short>(readBigEndian16(header + 12)); // 除数是负数

		std::cout << "文件头长度: " << headerLength << std::endl;
		std::cout << "MIDI 格式: " << format << std::endl;
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

		// 初始化通道
		channels.clear();
		channels.resize(16); // 最多 16 个 MIDI 通道

		// 遍历解析每个轨道
		for (unsigned int trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
			char trackHeader[8];
			if (fread(trackHeader, 1, 8, file) != 8) {
				std::cerr << "读取轨道头失败！" << std::endl;
				fclose(file);
				return false;
			}

			// 检查"MTrk"标识
			if (memcmp(trackHeader, "MTrk", 4) != 0) {
				std::cout << "警告: 轨道 " << trackIndex << " 不是标准 MTrk 轨道，跳过..." << std::endl;
				// 尝试读取长度然后跳过
				unsigned int skipLength = readBigEndian32(trackHeader + 4);
				fseek(file, skipLength, SEEK_CUR);
				continue;
			}

			// 安全地获取“轨道长度”
			unsigned int trackLength = readBigEndian32(trackHeader + 4);

			std::cout << "轨道 " << trackIndex << " 长度: " << trackLength << " 字节" << std::endl;

			// 检查“轨道长度”是否合理
			if (trackLength == 0) {
				std::cout << "警告: 轨道 " << trackIndex << " 长度为0，跳过..." << std::endl;
				continue;
			}

			if (trackLength > 50000000) { // 限制“轨道长度”的最大值为 50MB
				std::cerr << "轨道长度过大: " << trackLength << "，可能文件已损坏" << std::endl;
				fclose(file);
				return false;
			}

			// 读取轨道数据
			std::vector<unsigned char> trackData;
			try {
				trackData.resize(trackLength);
				size_t bytesRead = fread(trackData.data(), 1, trackLength, file);
				if (bytesRead != trackLength) {
					std::cerr << "读取轨道数据不完整！ 应该读取: " << trackLength
						<< " 实际读取: " << bytesRead << std::endl;
					// 继续处理已读取的数据
					trackData.resize(bytesRead);
				}
			}
			catch (const std::bad_alloc& e) {
				std::cerr << "内存分配失败，轨道长度: " << trackLength << " 错误: " << e.what() << std::endl;
				fclose(file);
				return false;
			}
			catch (const std::exception& e) {
				std::cerr << "创建轨道数据时发生异常: " << e.what() << std::endl;
				fclose(file);
				return false;
			}

			// 解析轨道事件
			if (!trackData.empty()) {
				parseTrackEvents(trackData, division);
			}

			std::cout << "解析轨道已完成 " << trackIndex << ", 当前 BPM: " << bpm << std::endl;
		}

		fclose(file);

		// 将所有音符按起始时间分组为和弦
		groupNotesIntoChords();

		// 准确统计有效通道数
		int validChannels = 0;
		int totalNotes = 0;
		int totalChords = 0;

		std::cout << "\n通道详细信息:" << std::endl;
		std::cout << "================" << std::endl;

		for (size_t i = 0; i < channels.size(); ++i) {
			int chordCount = channels[i].size();
			int noteCount = 0;
			for (const auto& chord : channels[i]) {
				noteCount += chord.notes.size();
			}
			totalNotes += noteCount;
			totalChords += chordCount;

			// 判断通道是否有效 - 使用更合理的标准
			// 有效通道标准: 至少有 1 个和弦或至少有 1 个音符
			if (chordCount > 0 || noteCount > 0) {
				std::cout << "通道 " << i << ": " << chordCount << " 个和弦, " << noteCount << " 个音符";
				std::cout << " [有效]" << std::endl;
				validChannels++;
			}
		}

		std::cout << "\n统计结果:" << std::endl;
		std::cout << "===========" << std::endl;
		std::cout << "总通道数: " << channels.size() << std::endl;
		std::cout << "有效通道数 (有和弦和音符): " << validChannels << std::endl;
		std::cout << "总和弦数: " << totalChords << std::endl;
		std::cout << "总音符数: " << totalNotes << std::endl;
		std::cout << "最终 BPM: " << bpm << std::endl;

		return true;
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
	double calculateChannelDifficulty(int channelIndex) {
		// 安全检查
		if (channelIndex < 0 || channelIndex >= (int)channels.size()) {
			return 0;
		}

		const auto& channelChords = channels[channelIndex];

		// 检查通道是否有足够的数据进行计算
		if (channelChords.size() <= 1) {
			return 0;  // 需要至少 2 个和弦
		}

		double totalDifficulty = 0; // 总难度
		int validTransitions = 0; // 记录有效的转换数

		// 从第二个和弦开始计算 (由于，第一个和弦的前面没有上一个和弦，故其难度为0)
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

	// 计算整体难度
	double calculateOverallDifficulty() {
		if (channels.empty()) return 0;

		double sumScaledDifficulties = 0;
		int validChannelCount = 0; // 实际有效的通道数

		std::cout << "\n各通道难度计算:" << std::endl;
		std::cout << "========================" << std::endl;

		// 计算每个通道的难度
		for (size_t i = 0; i < channels.size(); ++i) {
			// 判断通道是否有效 - 与上面使用相同的标准
			int chordCount = channels[i].size();
			int noteCount = 0;
			for (const auto& chord : channels[i]) {
				noteCount += chord.notes.size();
			}

			// 有效通道标准: 至少有 1 个和弦或至少有 1 个音符
			if (chordCount > 0 || noteCount > 0) {
				double channelDifficulty = calculateChannelDifficulty(i);
				std::cout << "通道 " << i << " 难度: " << std::fixed << std::setprecision(2)
					<< channelDifficulty << " (和弦数: " << chordCount
					<< ", 音符数: " << noteCount << ")" << std::endl;

				// 为难度计算，需要未缩放的值
				double scaledChannelDifficulty = channelDifficulty * 6.0;
				sumScaledDifficulties += scaledChannelDifficulty;
				validChannelCount++;
			}
		}

		// 如果没有有效通道，返回 0
		if (validChannelCount == 0) {
			std::cout << "没有有效通道，总难度: 0" << std::endl;
			return 0;
		}

		// 计算总体难度 (平均所有有效通道)
		double overallDifficulty = (sumScaledDifficulties / validChannelCount) / 6.0;
		std::cout << "有效通道数: " << validChannelCount << ", 平均难度: " << overallDifficulty << std::endl;

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