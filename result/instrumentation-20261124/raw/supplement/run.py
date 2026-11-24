"""Run serially from repository root after compiling the Release measurement driver."""
from pathlib import Path
import csv
import subprocess
import sys
folder = Path(__file__).resolve().parent
driver = Path(sys.argv[1]).resolve()
names = ['Finesse-Gear', 'NTransform-Gear']
with (folder/'throughput.csv').open('x', newline='') as stream:
    writer = csv.writer(stream)
    writer.writerow(['dataset','phase','round','order','algorithm','inputBytes','chunks','uniqueChunks','finalPayloadBytes','elapsedNs','cdcTimeUs','encodeTimeUs'])
    for i in range(1,5):
        config = folder.parent/'configs'/f'ds{i}-sf-benchmark.conf'
        for rep in range(6):
            order = list(reversed(names)) if rep and rep % 2 == 0 else names
            for j, name in enumerate(order, 1):
                row = subprocess.check_output([driver,config,name,'throughput'], text=True).strip().split(',')
                if len(row) != 7 or int(row[4]) <= 0:
                    raise ValueError(row)
                writer.writerow([i,'measure' if rep else 'warmup',rep,j,name,*row])
                stream.flush()
        with (folder/f'ds{i}-locality.csv').open('x') as out:
            subprocess.run([driver,config,names[1],'locality'],stdout=out,check=True)
        print(f'DataSet_{i} supplement complete',flush=True)
