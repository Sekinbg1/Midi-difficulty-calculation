import mido
from collections import defaultdict

def judge_midi_difficulty(midi_path: str) -> dict:
    """
    输入 MIDI 路径，返回难度评分与各项指标
    难度总分：0~100 → 映射为 1~10 星
    """
    mid = mido.MidiFile(midi_path)
    ticks_per_beat = mid.ticks_per_beat

    # 收集所有音符事件 (note_on, velocity>0)
    notes = []
    total_ticks = 0
    tempo = 500000  # 默认 120BPM

    for track in mid.tracks:
        track_ticks = 0
        for msg in track:
            track_ticks += msg.time
            total_ticks = max(total_ticks, track_ticks)
            if msg.type == 'set_tempo':
                tempo = msg.tempo
            if msg.type == 'note_on' and msg.velocity > 0:
                notes.append({
                    'note': msg.note,
                    'time': track_ticks,
                    'channel': msg.channel
                })

    # 无音符
    if not notes:
        return {'score': 0, 'level': 1, 'info': '空MIDI'}

    # 时长（秒）
    duration_sec = mido.tick2second(total_ticks, ticks_per_beat, tempo)
    duration_sec = max(duration_sec, 1)

    # ===== 各项难度指标 =====
    note_count = len(notes)
    note_density = note_count / duration_sec  # 每秒音符数

    # 音域
    pitches = [n['note'] for n in notes]
    pitch_min = min(pitches)
    pitch_max = max(pitches)
    pitch_range = pitch_max - pitch_min

    # 左右手分离（简单用 channel 0/1 近似）
    ch0 = [n for n in notes if n['channel'] == 0]
    ch1 = [n for n in notes if n['channel'] == 1]
    hand_range = 0
    if ch0 and ch1:
        p0 = [n['note'] for n in ch0]
        p1 = [n['note'] for n in ch1]
        hand_range = abs(max(p0) - min(p1))

    # 和弦密度（同一时间多个音符）
    time_notes = defaultdict(int)
    for n in notes:
        time_notes[round(n['time'] / 10)] += 1
    chord_count = sum(1 for t, c in time_notes.items() if c >= 3)
    chord_density = chord_count / duration_sec

    # 连续快速音（间隔 < 100ms）
    notes_sorted = sorted(notes, key=lambda x: x['time'])
    fast_run_count = 0
    for i in range(1, len(notes_sorted)):
        t_prev = notes_sorted[i-1]['time']
        t_curr = notes_sorted[i]['time']
        dt_sec = mido.tick2second(t_curr - t_prev, ticks_per_beat, tempo)
        if dt_sec < 0.1:
            fast_run_count += 1
    fast_density = fast_run_count / duration_sec

    # ===== 难度打分（0~100）=====
    score = 0
    score += min(note_density * 2, 30)
    score += min(pitch_range / 10, 20)
    score += min(hand_range / 15, 15)
    score += min(chord_density * 5, 20)
    score += min(fast_density * 3, 15)
    score = min(round(score, 1), 100)

    # 星级 1~10
    level = max(1, min(10, round(score / 10)))

    return {
        '总分(0-100)': score,
        '难度等级(1-10)': level,
        '总音符数': note_count,
        '时长(s)': round(duration_sec, 1),
        '每秒音符数': round(note_density, 1),
        '音域跨度': pitch_range,
        '左右手最大跨度': hand_range,
        '和弦密度': round(chord_density, 2),
        '快速音密度': round(fast_density, 2)
    }

if __name__ == '__main__':
    # 替换成你的 MIDI 文件路径
    midi_file = 'test.mid'
    result = judge_midi_difficulty(midi_file)
    for k, v in result.items():
        print(f'{k}: {v}')