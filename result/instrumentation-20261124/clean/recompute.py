import csv, math, statistics as st
from pathlib import Path
from collections import Counter
root=Path(__file__).resolve().parents[1]
raw=root/'raw'
clean=root/'clean'
def read(p):
    with p.open(newline='') as f: return list(csv.DictReader(f))
def save(file,rows):
    with (clean/file).open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
names=['Finesse-Gear','NTransform-Gear']
description=['5 份 Emacs tar.gz','5 版 Emacs 展开源码','5 份 Emacs tar','8 份 32 MiB 合成备份']
groups=[]; locality=[]; quality=[]; speed=[]; rounds=[]
system=read(raw/'supplement/throughput.csv')
def close(a,b):return math.isclose(float(a),float(b),rel_tol=1e-12,abs_tol=1e-15)
for i in range(1,5):
    ds=f'DataSet_{i}'; sf=raw/f'ds{i}-sf-benchmark'; delta=raw/f'ds{i}-delta-benchmark'
    for folder in (sf,delta):
        assert (folder/'run.txt').read_text().splitlines()[0]=='status=ok'
    for name in ['manifest.csv','source-checksums.csv','stored-chunks.csv','chunk-references.csv']:
        assert (sf/name).read_bytes()==(delta/name).read_bytes()
    d=read(delta/'deduplication.csv')[0]
    files=read(delta/'source-checksums.csv'); manifest=read(delta/'manifest.csv'); stores=read(delta/'stored-chunks.csv')
    pos=Counter()
    for r in manifest:
        assert pos[r['file']]==int(r['offset']);pos[r['file']]+=int(r['length'])
    assert all(pos[f['file']]==int(f['bytes']) for f in files)
    assert [int(x['storedId']) for x in stores]==list(range(len(stores)))
    n=sum(int(x['bytes']) for x in files);u=sum(int(x['length']) for x in stores)
    assert (n,u,len(manifest),len(stores))==tuple(int(d[k]) for k in ['inputBytes','uniqueBytes','inputChunks','uniqueChunks'])
    groups.append(dict(dataset=ds,kind='synthetic' if i==4 else 'real',files=len(files),inputBytes=n,sizeGB=n/1e9,uniqueBytes=u,chunks=len(manifest),uniqueChunks=len(stores),DR=n/u,workload=description[i-1]))
    pairs=read(raw/f'supplement/ds{i}-locality.csv'); att=[r for r in read(delta/'delta-attempts.csv') if r['algorithm']==names[1]]
    assert [(r['targetId'],r['baseId']) for r in pairs]==[(r['storedId'],r['baseId']) for r in att]
    totals=[]
    for count,mask in [('identicalCount','identicalMask'),('equalFeatureCount','equalFeatureMask')]:
        assert all(len(r[mask])==12 and set(r[mask])<={'1','0'} and int(r[count])==r[mask].count('1') for r in pairs)
        totals.append(sum(int(r[count]) for r in pairs))
    k=len(pairs); locality.append(dict(dataset=ds,candidatePairs=k,subchunksPerPair=12,identicalTotal=totals[0],equalFeatureTotal=totals[1],meanIdentical=totals[0]/k,meanEqualFeature=totals[1]/k))
    q=read(delta/'compression-summary.csv'); summ=read(sf/'summary.csv')
    assert [r['algorithm'] for r in q]==names
    for comp,summary in zip(q,summ):
        name=comp['algorithm'];assert summary['algorithm']==name
        keys=['uniqueOriginalBytes','finalPayloadBytes','rawChunks','deltaChunks','matchedAttempts','rawFallbacks','attemptedOriginalBytes','attemptedDeltaBytes','acceptedOriginalBytes','acceptedDeltaBytes','dcr','attemptedResidualRatio','attemptedSavingRatio','acceptedResidualRatio','acceptedSavingRatio','storedResidualRatio','storedSavingRatio','maxDeltaDepth','verifiedOccurrences','verifiedBytes']
        quality.append(dict(dataset=ds,algorithm=name,**{key:comp[key] for key in keys}))
        for metric in ('SF','system'):
            if metric=='SF':
                source=[r for r in read(sf/'measurements.csv') if r['algorithm']==name]
                assert len({r['outputDigest'] for r in source})==1
                spec=[(r['repetition'],r['order'],r['processedBytes'],r['elapsedNs']) for r in source]
            else:
                source=[r for r in system if int(r['dataset'])==i and r['algorithm']==name]
                assert len(source)==6 and sum(r['phase']=='warmup' for r in source)==1
                assert all((int(r['inputBytes']),r['finalPayloadBytes'],int(r['chunks']),int(r['uniqueChunks']))==(n,comp['finalPayloadBytes'],len(manifest),len(stores)) for r in source)
                spec=[(r['round'],r['order'],r['inputBytes'],r['elapsedNs']) for r in source if r['phase']=='measure']
            assert [int(t[0]) for t in spec]==list(range(1,6))
            vals=[]
            for rep,order,b,ns in spec:
                rate=int(b)*1000/int(ns);vals.append(rate)
                rounds.append(dict(dataset=ds,algorithm=name,metric=metric,repetition=rep,order=order,bytes=b,elapsedNs=ns,MBps=rate))
            mean=st.mean(vals)
            if metric=='SF':assert close(mean,summary['meanMBPerSecond'])
            speed.append(dict(dataset=ds,algorithm=name,metric=metric,n=5,meanMBps=mean,sdMBps=st.pstdev(vals),minMBps=min(vals),maxMBps=max(vals)))
save('table1-locality.csv',locality);save('table2-workloads.csv',groups);save('table3-compression.csv',quality)
save('figures4-5-rates.csv',speed);save('formal-rounds.csv',rounds)
labels=['WEB','TAR','RDB','SYN','VMA','VMB']
p1=[dict(dataset=l,meanIdentical=a,meanEqualFeature=b) for l,a,b in zip(labels,'8.27 9.19 6.86 5.78 5.99 6.34'.split(),'10.82 10.97 10.23 10.10 10.04 10.64'.split())]
p2=[dict(dataset=l,sizeGB=a,DR=b,workload=c) for l,a,b,c in zip(labels,[367,112,540,330,117,321],'4.21 1.70 12.25 13.07 1.61 10.45'.split(),['新浪新闻网站 135 天快照','258 个 Linux 内核版本，各自打为 tar','100 份 Redis 备份','176 份模拟文件创建、删除和修改的合成备份','78 个不同操作系统发行版的虚拟机镜像','研究组使用的 Ubuntu 12.04 虚拟机的 20 份备份'])]
p3=[]
for l,s in zip(labels,['7.60 .8749 7.52 .8795 -1.05 +0.53','15.00 .9516 15.34 .9846 +2.27 +3.47','3.67 .9129 3.94 .9448 +7.36 +3.49','1.75 .9326 1.70 .9640 -2.86 +3.37','1.56 .9088 1.51 .9161 -3.21 +0.80','1.30 .9093 1.28 .9193 -1.54 +1.10']):
    a,b,c,d,e,f=s.split()
    for alg,dcr,dce,rd,re in [('N-transform SF',a,b,'',''),('Finesse',c,d,e,f)]:
        p3.append(dict(dataset=l,algorithm=alg,DCR=dcr,DCE='0'+dce,DCRchangePercent=rd,DCEchangePercent=re))
for filename,rows in [('paper-table1.csv',p1),('paper-table2.csv',p2),('paper-table3.csv',p3)]:
    save(filename,rows)
print('verified all formal rounds, recovery summaries, pairs, source ranges and unique IDs')

# 保留实验 ID 与阶段口径，使清洗表可回连原始统计。
import json

stages, resources, indices, storage, recovery, encoding, comparisons = [], [], [], [], [], [], []
for dataset in range(1, 5):
    for kind in ("sf", "delta"):
        reference = None
        for mode in ("benchmark", "profiling"):
            ds = f"DataSet_{dataset}"
            path = raw / f"ds{dataset}-{kind}-{mode}"
            labels = dict(dataset=ds, entry=kind, mode=mode)
            config = json.loads((path / "experiment_config.json").read_text())
            summary = json.loads((path / "summary.json").read_text())
            assert summary["complete"] and config["mode"] == mode
            assert summary["wall_time_us"] >= summary["processing_wall_time_us"]
            assert all((path / name).is_file() for name in summary["outputs"])
            stages_here = read(path / "stage_timing.csv")
            assert all(r["experiment_id"] == summary["experiment_id"] for r in stages_here)
            for stage in stages_here:
                assert close(float(stage["total_time_us"]) / 1000, stage["total_time_ms"])
            stages.extend(dict(labels, **row) for row in stages_here)
            for filename, target in [("system_metrics.csv", resources), ("index_metrics.csv", indices),
                                     ("storage_metrics.csv", storage)]:
                if (path / filename).exists():
                    target.extend(dict(labels, **row) for row in read(path / filename))
            if kind == "sf":
                deterministic = [(r["algorithm"], r["repetition"], r["order"], r["outputDigest"])
                                 for r in read(path / "measurements.csv")]
                old = root.parent / "legacy-20261123" / "raw" / f"ds{dataset}-sf" / "measurements.csv"
                if old.exists():
                    assert deterministic == [(r["algorithm"], r["repetition"], r["order"], r["outputDigest"])
                                             for r in read(old)]
            else:
                encoded = (path / "encoded-chunks.csv").read_bytes()
                restored = (path / "restoration-verification.csv").read_bytes()
                files = read(path / "file-restoration.csv")
                deterministic = (encoded, restored, files)
                old = root.parent / "legacy-20261123" / "raw" / f"ds{dataset}-delta"
                if old.exists():
                    assert encoded == (old / "encoded-chunks.csv").read_bytes()
                    assert restored == (old / "restoration-verification.csv").read_bytes()
                for name in names:
                    audit = [r for r in files if r["algorithm"] == name]
                    comp = next(r for r in read(path / "compression-summary.csv") if r["algorithm"] == name)
                    assert len(audit) == summary["files"] and all(r["status"] == "ok" for r in audit)
                    assert sum(int(r["restored_bytes"]) for r in audit) == summary["source_bytes"]
                    recovery.append(dict(labels, algorithm=name, verified_files=len(audit),
                                         verified_occurrences=comp["verifiedOccurrences"],
                                         verified_bytes=comp["verifiedBytes"]))
                    # 逐块旧纳秒记录应与新版同一区间的微秒累计一致。
                    attempts = [int(r["encodeNs"]) for r in read(path / "delta-attempts.csv")
                                if r["algorithm"] == name]
                    stage = next((r for r in stages_here if r["algorithm"] == name
                                  and r["stage_name"] == "delta_encode"), None)
                    assert len(attempts) == int(comp["matchedAttempts"])
                    assert sum(attempts) == int(comp["encodeNs"])
                    if attempts:
                        assert stage and len(attempts) == int(stage["call_count"])
                        assert abs(float(stage["total_time_us"]) - sum(attempts) / 1000) <= len(attempts) / 1000
                    else:
                        assert stage is None
                    encoding.append(dict(labels, algorithm=name, attempts=len(attempts),
                                         encode_total_us=stage["total_time_us"] if stage else "",
                                         encode_total_ms=stage["total_time_ms"] if stage else "",
                                         mean_us=stage["average_time_us"] if stage else "",
                                         p50_us=stage["p50_time_us"] if stage else "",
                                         p95_us=stage["p95_time_us"] if stage else "",
                                         p99_us=stage["p99_time_us"] if stage else ""))
            if reference is None:
                reference = deterministic
            else:
                assert deterministic == reference
            if mode == "profiling" and kind == "delta":
                # 检查选中明细行与阶段聚合可逐项复算。
                ops = read(path / "operation_metrics.csv")
                for name in names:
                    for stage_name in ("delta_encode", "roundtrip_verify", "sf_lookup"):
                        operation = [float(r["duration_us"]) for r in ops
                                     if r["phase"] == "build" and r["algorithm"] == name
                                     and r["operation_type"] == stage_name]
                        stage = next((r for r in stages_here if r["phase"] == "build"
                                      and r["algorithm"] == name and r["stage_name"] == stage_name), None)
                        if operation:
                            assert stage and len(operation) == int(stage["call_count"])
                            assert close(sum(operation), stage["total_time_us"])
                        else:
                            assert stage is None
        comparisons.append(dict(dataset=ds, entry=kind, benchmark_equals_profiling="true",
                                matches_prior_non_timing="true" if old.exists() else ""))
for filename, rows in [("stage-timing.csv", stages), ("resources.csv", resources), ("indices.csv", indices),
                       ("storage.csv", storage), ("recovery.csv", recovery), ("delta-encoding.csv", encoding),
                       ("reproducibility.csv", comparisons)]:
    save(filename, rows)

# 从清洗表构建同组同输入的对照，百分比基线为 NTransform-Gear。
trend = []
for group in groups:
    ds = group["dataset"]
    fn, nn = [r for r in quality if r["dataset"] == ds]
    fl, nl = [r for r in speed if r["dataset"] == ds and r["metric"] == "SF"]
    ft, nt = [r for r in speed if r["dataset"] == ds and r["metric"] == "system"]
    trend.append(dict(dataset=ds, sf_speedup=fl["meanMBps"]/nl["meanMBps"],
                      pipeline_speedup=ft["meanMBps"]/nt["meanMBps"],
                      dcr_change_pct=100*(float(fn["dcr"])/float(nn["dcr"])-1),
                      payload_gap_bytes=int(fn["finalPayloadBytes"])-int(nn["finalPayloadBytes"]),
                      finesse_covered_pct=100*int(fn["attemptedOriginalBytes"])/int(fn["uniqueOriginalBytes"]),
                      ntransform_covered_pct=100*int(nn["attemptedOriginalBytes"])/int(nn["uniqueOriginalBytes"])))
save("trend-comparison.csv", trend)
print("verified modes, legacy compatibility, stage units, operation totals, file recovery and wall-clock boundaries")
