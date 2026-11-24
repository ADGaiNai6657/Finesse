from pathlib import Path
import html,csv,json,statistics
from reportlab.graphics.shapes import Drawing
from reportlab.graphics.charts.barcharts import VerticalBarChart
from reportlab.graphics import renderSVG
from reportlab.lib import colors
from reportlab.graphics.shapes import String,Rect
root=Path(__file__).resolve().parents[1]
def read(s):
    with (root/'clean'/s).open() as f:return list(csv.DictReader(f))
G=read('table2-workloads.csv'); L=read('table1-locality.csv'); Q=read('table3-compression.csv'); S=read('figures4-5-rates.csv')
F='Finesse-Gear';N='NTransform-Gear'
def rate(ds,metric,a,key='meanMBps'):
    return float(next(r[key] for r in S if (r['dataset'],r['metric'],r['algorithm'])==(ds,metric,a)))
for m,idx in [('SF',4),('system',5)]:
    g=Drawing(510,222)
    bar=VerticalBarChart();bar.x=52;bar.y=35;bar.width=435;bar.height=143
    bar.data=[[rate(r['dataset'],m,a) for r in G] for a in (N,F)]
    bar.categoryAxis.categoryNames=[r['dataset'] for r in G]
    bar.valueAxis.valueMin=0;bar.valueAxis.valueMax,bar.valueAxis.valueStep=(2500,500) if idx==4 else (800,200)
    bar.bars[0].fillColor=colors.lightgrey;bar.bars[1].fillColor=colors.black
    g.add(bar);g.add(String(10,199,'MB/s',fontSize=10))
    for j,(name,col) in enumerate([(N,colors.lightgrey),(F,colors.black)]):
        x=155+125*j;g.add(Rect(x,195,10,9,fillColor=col));g.add(String(x+14,195,name,fontSize=10))
    renderSVG.drawToFile(g,str(root/'clean'/f'figure{idx}.svg'))
md=[];ht=[]; esc=lambda x:html.escape(str(x))
def heading(level,s):md.append('#'*level+' '+s);ht.append(f'<h{level}>{esc(s)}</h{level}>')
def p(s):md.append(s);ht.append('<p>'+esc(s)+'</p>')
def links(arr):
    md.append(' · '.join(f'[{a}]({b})' for a,b in arr));ht.append('<p>'+' · '.join(f'<a href="{esc(b)}">{esc(a)}</a>' for a,b in arr)+'</p>')
def img(alt,url):
    md.append(f'![{alt}]({url})');ht.append(f'<p><img alt="{esc(alt)}" src="{esc(url)}"></p>')
def table(header,data):
    data=list(data)
    def line(row):return '| '+' | '.join(map(str,row))+' |'
    md.append('\n'.join([line(header),line(['---']*len(header))]+[line(row) for row in data]))
    ht.append('<table border="1"><thead><tr>'+''.join('<th>'+esc(x)+'</th>' for x in header)+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+esc(x)+'</td>' for x in r)+'</tr>' for r in data)+'</tbody></table>')
def fmt(x,n=4):return f'{float(x):.{n}f}' if x!='' else '—'
heading(1,'Finesse 新版统计实验 · 2026-11-24')
config=json.loads((root/'raw/ds1-delta-benchmark/experiment_config.json').read_text())
p(f"Apple M5 单线程 Release。构建记录的 Git 提交为 {config['git_commit'][:7]}，本次统计改动来自工作区，实际源码由 raw 中的 source-files.csv 和 SHA-256 标识。四组输入共 17,000 个文件、1,734,815,694 字节。每组分别完成 SF 与 Delta 的 Benchmark 和 Profiling，各模式的算法结果逐项复核。")
links([('本次输入字节归档','../raw/source-inputs.tar'),('清洗与复核脚本','clean/recompute.py'),('逐轮测速','clean/formal-rounds.csv'),('论文原版 PDF','paper/FAST19-Finesse.pdf'),('本次文件 SHA-256','SHA256SUMS'),('上次实验报告','../report.html')])
trend=read('trend-comparison.csv')
sf_ratios=[float(r['sf_speedup']) for r in trend]
system_ratios=[float(r['pipeline_speedup']) for r in trend]
p(f"Finesse-Gear 的 SF 速度为基线的 {min(sf_ratios):.2f}–{max(sf_ratios):.2f} 倍。单算法内存管线吞吐为 {min(system_ratios):.2f}–{max(system_ratios):.2f} 倍。展开源码和未压缩 tar 的 Finesse-Gear DCR 仍低 16.77% 和 16.50%。新旧记录的 SF 输出摘要、最终 Raw/Delta 表示、逐块恢复证据逐项相符。")
links([('逐组趋势','clean/trend-comparison.csv'),('结果一致性复核','clean/reproducibility.csv')])
heading(2,'表 1　对应子块的局部性')
p('每个样本是 NTransform-Gear 实际选中的一个目标块与 Raw 基块对，包含所有匹配尝试。按各自长度等分 12 段，对应位置比较 SHA-1 和 Finesse-Gear 子块最大指纹。两行均值各以候选对数作分母，取值为 0 到 12。第 1 组只有一个样本。')
table(['指标']+[r['dataset'] for r in L],[[label]+[fmt(r[k],3) if k.startswith('mean') else r[k] for r in L] for label,k in [('候选对数','candidatePairs'),('平均内容相同子块数','meanIdentical'),('平均 feature 相同子块数','meanEqualFeature')]])
links([('清洗表 1','clean/table1-locality.csv')]+[(f'第 {i} 组逐对原值',f'raw/supplement/ds{i}-locality.csv') for i in range(1,5)])
heading(2,'表 2　工作负载')
table(['数据集','来源','文件数','大小 GB','输入字节','唯一字节','DR','内容'],[[r['dataset'],r['kind'],r['files'],fmt(r['sizeGB'],6),r['inputBytes'],r['uniqueBytes'],fmt(r['DR']),r['workload']] for r in G])
p('GB 使用十进制。DR 为输入字节除以精确去重后的唯一字节。第 4 组使用 FastCDC 已有的 8 份合成备份，生成种子为 20260917。')
links([('清洗表 2','clean/table2-workloads.csv'),('输入清单索引','raw/configs/input-lists/inventory.csv')])
heading(2,'表 3　去重后的 Delta 压缩效果')
p('DCR 为唯一块原始字节总数除以最终 Raw 与 Delta 的 payload 字节总数。论文对 DCE 同时使用“压缩后/压缩前”和“越高越好”两种描述。本表对全部匹配尝试分别给出剩余比例与节省比例，均按字节加权；无匹配时分母为零，以 — 表示。变化率以同组 NTransform-Gear 为基线。')
data=[]
for g in G:
    a,b=[r for r in Q if r['dataset']==g['dataset']]
    for r in (b,a):
        change='基线' if r==b else f"{(float(r['dcr'])/float(b['dcr'])-1)*100:+.2f}%"
        data.append([r['dataset'],r['algorithm'],r['matchedAttempts'],r['deltaChunks'],r['finalPayloadBytes'],fmt(r['dcr']),change,fmt(r['attemptedResidualRatio']),fmt(r['attemptedSavingRatio'])])
table(['数据集','算法','匹配','接受 Delta','最终字节','DCR','DCR 变化','匹配剩余比例','匹配节省比例'],data)
links([('清洗表 3（含尝试、接受和全体逐项分子分母）','clean/table3-compression.csv')])
for metric,idx,name in [('SF',4,'SF 计算速度'),('system',5,'内存管线吞吐')]:
    heading(2,f'图 {idx}　{name}')
    if idx==4:p('输入为精确去重后的唯一块，排除短于 12 字节的块。计时从开始提取到写完预分配的签名结果。速度分子为本轮处理的有效唯一字节。')
    else:p('每算法、每轮独立进程，启用新版 Benchmark 聚合。计时从读取输入列表、源文件开始，包含文件 SHA-256、CDC、SHA-1 精确去重、SF/索引、Xdelta、即时解码核验，到内存编码池构建完成。分子为全部输入字节；配置解析、CSV 导出、释放原文后的恢复遍历在计时区间外。使用系统正常文件缓存，未清缓存。')
    p('各算法预热一次，五轮正式测量，算法顺序按轮交替。MB/s = 字节 × 1000 / 纳秒。下表为五个逐轮速度的算术平均值 ± 总体标准差，倍数取两算法平均速度之比。')
    img(f'本机图 {idx}，五轮平均 MB/s',f'clean/figure{idx}.svg')
    table(['数据集','NTransform-Gear MB/s','Finesse-Gear MB/s','Finesse / NTransform'],[[r['dataset']]+[f"{rate(r['dataset'],metric,a):.2f} ± {rate(r['dataset'],metric,a,'sdMBps'):.2f}" for a in (N,F)]+[f"{rate(r['dataset'],metric,F)/rate(r['dataset'],metric,N):.3f}×"] for r in G])
links([('逐轮正式记录','clean/formal-rounds.csv'),('平均值、标准差及范围','clean/figures4-5-rates.csv'),('含预热的系统吞吐原始记录','raw/supplement/throughput.csv')])
heading(2,'新版阶段统计与资源')
p('下表取 Delta Benchmark 的单次实跑。输入阶段同一轮只执行一次。耗时以毫秒（ms）表示。file_read 为实际源文件读取，source_sha256 为输入完整性散列。dedup_total 已包含块指纹、索引查找与插入。')
stage_rows=read('stage-timing.csv')
def stage_ms(dataset, algorithm, stage, phase):
    for row in stage_rows:
        if (row['dataset'],row['algorithm'],row['stage_name'],row['phase'],row['entry'],row['mode']) == (dataset,algorithm,stage,phase,'delta','benchmark'):
            return fmt(row['total_time_ms'],2)
    return '—'
table(['数据集','源读取 ms','源 SHA-256 ms','CDC ms','去重 ms'],[
    [g['dataset']]+[stage_ms(g['dataset'],alg,stage,'input') for alg,stage in
        [('shared','file_read'),('shared','source_sha256'),('FastCDC','cdc_total'),('shared','dedup_total')]]
    for g in G])
p('编码池构建包含 SF、候选索引、目标和基块摘要、编码与即时回放。构建中的子阶段时长已经包含在构建列里。逐出现块恢复在构建后运行，整文件流式 SHA-256 校验复用同一次恢复的字节。')
spec=[('encoded_pool_build','build'),('feature_extraction','build'),('sf_lookup','build'),('delta_encode','build'),('roundtrip_verify','build'),('chunk_recovery','recovery'),('file_reassembly','recovery'),('file_sha256_verify','recovery')]
table(['数据集','算法','构建 ms','SF ms','查找 ms','Delta 编码 ms','即时回放 ms','逐块恢复 ms','文件摘要累积 ms','文件摘要核对 ms'],[
    [g['dataset'],alg]+[stage_ms(g['dataset'],alg,stage,phase) for stage,phase in spec]
    for g in G for alg in (F,N)])
p('Profiling 提供逐次操作与延迟分位数。分位数取每阶段最早 4,096 个观测，累计与平均值覆盖全部调用。第 1 组 Finesse 没有匹配候选，Delta 编码耗时显示为缺失。累计阶段会嵌套，整组墙钟时长以 system_metrics 为准。整组墙钟包含两个算法、完整验证和主要统计文件写出。两种采集模式的单次墙钟只提供资源观测。')
resource=read('resources.csv')
table(['数据集','入口','模式','处理墙钟 s','含主要导出墙钟 s','CPU 时间 s','峰值 RSS MiB'],[
    [x['dataset'],x['entry'],x['mode'],fmt(float(x['processing_wall_time_us'])/1e6,3),fmt(float(x['wall_time_us'])/1e6,3),
     fmt(float(x['cpu_time_us'])/1e6,3) if x['cpu_time_us'] else '—',
     fmt(int(x['process_peak_rss_bytes'])/1048576,1) if x['process_peak_rss_bytes'] else '—'] for x in resource])
links([('全部阶段','clean/stage-timing.csv'),('编码分布','clean/delta-encoding.csv'),('资源','clean/resources.csv'),('索引','clean/indices.csv'),('存储','clean/storage.csv'),('文件和块恢复','clean/recovery.csv')])
heading(2,'论文原表与原图')
p('Zhang 等，FAST ’19，论文集 pp. 121–126。下列图片直接截自仓库原版 PDF。原表 1–3 同时转录为 CSV，保留原表 3 印出的 DCR、DCE 和百分比。论文图 4、5 的逐柱精确数值未刊载，保留原图；正文报告的 SF 平均加速为 i7 3.5×、E5 3.2×，系统吞吐提高 41%–85%。原始六数据集为 WEB、TAR、RDB、SYN、VMA、VMB。')
for i,label in enumerate(['对应子块','六组工作负载','DCR 与 DCE'],1):
    img(f'论文原表 {i} · {label}',f'paper/table{i}.png');links([(f'原表 {i} CSV',f'clean/paper-table{i}.csv')])
for i,label in enumerate(['Delta 处理流程','对应子块局部性','feature 分组示例','两种处理器的 SF 计算速度','两种处理器的系统吞吐'],1):
    img(f'论文原图 {i} · {label}',f'paper/figure{i}.png')
p('本机测量使用 FastCDC、Gear、内存基块和最大 Delta 深度 1。论文使用 Rabin、Destor 流水线、400 MB LRU 基块缓存及 i7-4770/E5-2620 平台。两套数据的工作负载、算法配置和计时条件各见对应表图。')
heading(2,'复核与原始材料')
p('源代码由 AppleClang 21.0.0 以 Release 构建，CTest 7/7 通过。独立核验逐项比对了输入 SHA-256、manifest 范围、去重映射、候选与恢复摘要。每种模式和每个算法均核对 178,002 次块出现和 17,000 份文件的 SHA-256。每轮补测的输入字节、块数、唯一块数及最终 payload 字节与正式 Delta 实验相符。新旧耗时受采集口径影响，报告用新版同模式内的结果比较算法。')
p('本次逐文件核对四组输入与上次归档中的 SHA-256，字节一致，因此报告直接链接到上次的原始输入 tar。原始目录中的 manifest、候选尝试、恢复摘要和逐轮计时保留程序输出格式。配置的输入清单使用本机绝对路径，清单内及运行输出中的源路径为相对路径。实验源位于仓库同级 FastCDC/Dataset。')
table(['数据集','SF 原始计时','Delta 质量','输入摘要'],[[r['dataset'],f'raw/ds{i}-sf-benchmark/measurements.csv',f'raw/ds{i}-delta-benchmark/compression-summary.csv',f'raw/ds{i}-delta-benchmark/source-checksums.csv'] for i,r in enumerate(G,1)])
for i in range(1,5):links([(f'第 {i} 组 SF 运行记录',f'raw/ds{i}-sf-benchmark/run.txt'),('Delta 运行记录',f'raw/ds{i}-delta-benchmark/run.txt'),('候选尝试',f'raw/ds{i}-delta-benchmark/delta-attempts.csv'),('恢复校验',f'raw/ds{i}-delta-benchmark/restoration-verification.csv')])
for i in range(1,5):
    links([(f'第 {i} 组 SF Profiling 阶段',f'raw/ds{i}-sf-profiling/stage_timing.csv'),
           ('Delta Profiling 明细',f'raw/ds{i}-delta-profiling/operation_metrics.csv'),
           ('整文件恢复证据',f'raw/ds{i}-delta-benchmark/file-restoration.csv')])
links([('输入归档复核脚本','clean/verify_archive.py'),('报告生成脚本','clean/render_report.py')])
links([('CTest 日志','raw/ctest.log'),('四组独立审计','raw/delta-audit.txt'),('原始输入归档核对','raw/archive-audit.txt'),('清洗校验','raw/cleaning-audit.txt'),('补测计时代码','raw/supplement/measure.cpp'),('轮次调度','raw/supplement/run.py')])
(root/'report.md').write_text('\n\n'.join(md)+'\n')
(root/'report.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Finesse 实验记录</title><body>\n'+'\n'.join(ht)+'\n</body></html>\n')
