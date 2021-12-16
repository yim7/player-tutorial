with open('11.wav', 'rb') as f1:
    data1 = [b for b in f1.read()]
    print(data1)
    
with open('sound.wav', 'rb') as f2:
    data2 = [b for b in f2.read()]
    print(data2)
assert data1 == data2