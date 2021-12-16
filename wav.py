import math


def save_wav(filename: str, sample_rate: int, sample_width: int, channels: int, samples: list[int]):
    data_length = len(samples) * sample_width

    with open(filename, 'wb') as f:
        f.write(b'RIFF')
        f.write((data_length + 36).to_bytes(4, 'little', signed=True))
        f.write(b'WAVE')
        f.write(b'fmt ')
        f.write((16).to_bytes(4, 'little'))
        f.write((1).to_bytes(2, 'little'))
        f.write((channels).to_bytes(2, 'little'))
        f.write((sample_rate).to_bytes(4, 'little'))
        f.write((sample_rate * sample_width * channels).to_bytes(4, 'little'))
        f.write((sample_width * channels).to_bytes(2, 'little'))
        f.write((sample_width * 8).to_bytes(2, 'little'))
        f.write(b'data')
        f.write(data_length.to_bytes(4, 'little'))

        for sample in samples:
            f.write(sample.to_bytes(sample_width, 'little'))


samples = []
for i in range(100000):
    samples.append(0)
    samples.append(255)

save_wav('sound.wav', 44100, 1, 1, samples)
