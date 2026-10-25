# Lab2 提示词与SPEC

## 1. page_init 物理内存初始化提示词

```text
请阅读ucore RISC-V Lab2的pmm.c、pmm.h、memlayout.h、riscv.h、dtb.h及链接脚本，
仅实现static void page_init(void)，不改动官方测试。
背景：dtb_init已经解析DTB，page_init使用get_memory_base/get_memory_size结果。
目标：正确设置va_pa_offset、npage、pages，输出RAM范围，保护固件、内核和描述符，
将剩余完整页交给当前pmm_manager的init_memmap。
约束：区分VA、PA、PPN和Page描述符；核对nbase是否已在框架声明，不能重复定义或使用未声明标识符。
当前工程仅支持DRAM_BASE起始的单段RAM，其他base明确panic，不伪称支持任意布局。
拒绝size=0和base+size溢出；上界取DTB末端与PHYSICAL_MEMORY_END较小者，向下页对齐。
按框架固定高地址映射设置偏移，npage是物理页号上界，描述符数量为npage-nbase。
pages置于end之后向上页对齐位置；先检查整个数组能放入RAM，避免乘加溢出。
逐页初始化ref/flags/property并标Reserved；空闲区从描述符末端向上页对齐后开始。
内存信息无效或元数据放不下时panic；没有完整空闲页时不得调用init_memmap。
使用框架的PADDR、PPN、ROUNDUP、ROUNDDOWN、SetPageReserved等真实宏，先读定义再用。
输出完整函数及必要的简短解释，不覆盖其他函数。验收：交叉编译、启动输出、原Page Init评分。
```

## 2. Best-Fit 分配器提示词

```text
阅读default_pmm.c、best_fit_pmm.c、pmm.h和list.h，在lab2/YOUR CODE允许区域
实现完整Best-Fit管理器。basic_check和best_fit_check两个测试函数必须逐字保留。
文件没有管理骨架，需要自行定义free_area及测试使用的free_list/nr_free状态约定，
实现init/init_memmap/alloc_pages/free_pages/nr_free_pages并注册const best_fit_pmm_manager。
pmm_manager实际还包含void (*check)(void)，必须读取完整声明并注册原best_fit_check，不能漏掉。
空闲链表按地址排序，块首页property存页数，正确维护PageProperty和ref。
分配遍历全部合格块选最小者，可精确匹配早停；拆分余块放回正确位置并维护计数。
释放按地址插回，仅物理连续块能合并，正确清除被合并旧块标记。
所有尾插分支插入后退出循环，避免重新遍历新节点；不允许重复入链。
在pmm.c的init_pmm_manager接入best_fit_pmm_manager，并保留当前USE_BUDDY条件选择，
默认启动应打印best_fit_pmm_manager；不能只实现文件而仍运行First-Fit。
零页请求遵守已有assert(n>0)约定，超大或无合适连续块返回NULL且状态不变。
输出允许区域的完整实现、必要include与明确的接入改动；保留工具链设置。
验收：原basic_check/best_fit_check、官方20/20及混合申请释放后计数恢复，不修改评分脚本。
```

## 3. Buddy System 实现prompt（第一份，组工程版本）

```text
请阅读当前ucore RISC-V的pmm.h、Page字段、list.h及页号转换，从零设计buddy_pmm.c/h。
适配包含check回调的完整pmm_manager，不预设Buddy框架或测试已经存在。
使用BUDDY_MAX_ORDER=31与分阶free_area_t桶，property保存空闲块页数2^k，另维护总空闲页数。
当前128MiB平台nbase=0x80000；按base-pages下标对齐拆分初始非2幂区间，并在释放时
使用同一下标xor(1<<order)寻伙伴。此原点与当前可用阶的物理对齐相容；移植其他RAM布局应重审。
申请n向上取2幂，从最低可满足阶取块并逐级二分，扣实际块大小；0页与超量请求返回NULL。
释放接收原申请n并向上取整整块回收，仅合并同阶空闲真伙伴，不能合并任意相邻块。
维护Property、Reserved、ref及每桶计数和总计数；不承诺部分释放或完整错误指针诊断。
输出完整模块、extern头声明与内核接入；默认Best-Fit，USE_BUDDY选择Buddy，PMM=buddy映射宏。
配套测试在下一份prompt独立设计，最终通过check回调运行，不改官方Best-Fit测试。
说明算法、边界与编译方式，保留组员署名及本机工具链配置，不虚构历史迭代。
```

## 4. Buddy System 测试prompt（第二份，组工程版本）

```text
针对前一份实际生成的Buddy接口，设计static void buddy_check(void)，在buddy_pmm.c内
实现并注册check；与实现逻辑分开，使用assert检验，不修改实现绕过失败。
先设计buddy_check_state：逐桶检查块Property、大小、下标对齐、每桶计数与总计数，
并验证同阶可合并伙伴不同时空闲。
覆盖：
1. 三次单页申请互不相同、ref为0、不在空闲桶；释放后恢复计数。
2. 3页请求占4页、下标4页对齐，整块不在空闲桶，free_pages(p,3)恢复。
3. 找到低阶空桶和更高非空桶，申请触发分裂，检查余伙伴留在低阶桶。
4. 同一大块的两半分别释放，先释放一半不可合并，第二半释放后逐级合并回原高阶块。
5. 0页与总空闲页+1返回NULL；临时保存桶头及计数、隐藏所有空闲块后验证耗尽失败，
   必须完整恢复原结构。这是模拟空桶，不声称逐页实际耗尽RAM。
6. 汇总所有空闲块页数等于每桶记录、总计数及nr_free_pages。
7. 同时申请1/2/3/5/7页，实际占1/2/4/8/8页，检查两两不重叠并交叉释放恢复。
测试失败assert，成功由check_alloc_page输出成功，不要求打印个人版本的四条Buddy PASS。
说明每组断言的有效性；不声称做了独立buddy_check.c、1000轮或逐页seen耗尽测试，
这些属于另一份个人学习工程。当前正常流程不故意测试非法释放导致panic。
```

## 5. 新增SLUB：SPEC

- 背景/输入：现有ucore RISC-V Lab2，已通过page_init、Best-Fit与Buddy测试；可用defs.h、pmm.h、list.h、page2pa、va_pa_offset、alloc_pages/free_pages、nr_free_pages。
- 目标/输出：从零实现教学版SLUB式对象层slub.c/h，独立测试slub_check.c，并按宏接入，不替代页管理器。
- 约束：单核；最小16字节对齐；16～2048大小类；大对象使用包含元数据的连续页；最大64KiB；空slab立即回收；不占用Page原有策略字段；明确0请求、失败与非法释放约定；不修改官方测试；保留当前工具链配置。
- 验收：两种底层均可编译启动；对象区间不重叠、写读一致、复用正确；失败不改变状态；测试后空闲页和对象计数恢复；课程QEMU4.1.1最终运行证据由本人取得。

以下两份为本次设计整理的可复现提示词，供报告提交与复核；不是虚构的逐字历史聊天或未经记录的迭代次数。

## 6. SLUB实现prompt（第一份）

```text
你是协助我完成ucore RISC-V Lab2 Challenge的编程助手。

请先阅读当前工程的defs.h、pmm.h、memlayout.h、list.h、pmm.c和Makefile，
在没有SLUB骨架的前提下新增kern/mm/slub.c、slub.h，独立设计对象分配模块。

功能：
1. 两层结构：下层继续调用已有alloc_pages/free_pages，上层提供按字节的slub_alloc。
2. 小对象按16、32、64、128、256、512、1024、2048字节分类；每个slab用1页，
   元数据和位图放在页头，剩余区域切成16字节对齐的对象。明确元数据占空间。
3. 空闲对象内保存下一空闲对象指针，分配期间调用者可写其全部可用对象区。
4. 超过2048字节用单独的连续页，页数含元数据与对齐；支持至64KiB，不承诺无限大。
5. 兼容Best-Fit和Buddy；记录原请求页数和实际消耗页数，释放向下层传原请求页数，
   防止Buddy向上取整后记账错误或部分回收。
6. 最后一个对象释放即归还slab页；大对象释放归还连续块；不保留空slab。
7. 提供slub_init、slub_alloc、slub_free、slub_usable_size、slub_live_objects、
   slub_backing_pages接口。init幂等，未初始化时分配返回NULL。
8. 分配0、超过上限、最大整数请求返回NULL且不改变状态；free(NULL)成功无操作；
   无效、偏移或当前可识别的重复释放返回-1，成功返回0。
9. 先在已登记slab清单里查归属，再读取元数据，不直接解引用任意传入地址。

约束：
- 教学单核实现，无并发锁/per-CPU/NUMA/构造函数，不声称完全等价于Linux SLUB。
- 不修改Page的property/flags来保存对象元数据，避免破坏原分配器。
- 不承诺完整UAF/ABA检测，不支持realloc或额外任意对齐。
- 原basic_check、best_fit_check和评分断言必须保持原样。
- 默认基础启动不启用SLUB；USE_SLUB宏下在check_alloc_page之后调用对象初始化和测试。
- 用make qemu-slub、qemu-slub-buddy分别测试，目标先clean避免编译宏污染。
- 安装保留现有ISA/QEMU/GDB选项并备份变更。检查原Best-Fit尾插后是否缺少break，
  仅在该模式确实存在时补充循环退出，不覆盖整个文件。

输出完整模块、可重复执行的安全接入脚本、修改清单与接口说明。
将测试放在另一份文件，不能只在实现里打印成功。
```

## 7. SLUB测试prompt（第二份）

```text
针对上一份实际生成的SLUB接口与错误约定，新增kern/mm/slub_check.c。
测试需使用assert或等价的结果校验，不能只检查非NULL、只打印PASS，不能篡改实现来绕过失败。

请覆盖：
1. 0、64KiB+1、(size_t)-1返回NULL，NULL释放成功，失败前后计数不变。
2. 1、15/16/17、31/32/33等大小类两侧边界，一直到2047/2048/2049、4095/4096/4097、
   8192、65535、65536。检查16字节对齐和usable_size符合接口。
3. 同时保留不同大小对象，逐字节写不同模式并读回，检查两两请求区间不重叠。
4. 同一slab两个64字节对象，释放一个不能回收整页，下一分配应复用该槽；
   测试偏移指针、未知指针、重复释放均被拒绝且不破坏状态。
5. 600个16字节对象，跨至少3个slab；先释放偶数序号再释放奇数，最后页数恢复。
6. 50轮混合大小和不同释放顺序，检查数据、对象数、占用页数及底层空闲页数恢复。
7. 用有上限的Page指针记录数组耗尽底层页，避免在内核栈创建巨型数组。
   已有slab有空槽时仍可分配；新大小类和大对象申请应失败，无额外计数变化；
   释放所有保留块和对象后恢复初始状态。

每组输出独立PASS，末尾输出slub_check() succeeded及恢复后的空闲页数。
按Best-Fit和Buddy两种底层运行；正常通过流程不故意触发不可恢复的页分配器assert。
解释各测试能发现什么错误、不能证明什么；记录开发验证与本人QEMU4.1.1结果的区别。
```

## 8. 新增真实验证记录（个人与最终组工程区分）

- 本次额外随机复测发现Best-Fit尾插缺少break，已针对性修复；官方测试保持原样。
- SLUB两种底层均已通过开发环境六项内核测试及各12000次宿主随机数据/地址对照检查。
- 开发环境QEMU为8.2.2；另于2026-10-25完成本人QEMU4.1.1/OpenSBI v0.4实测，两种环境分别记录。
- 本人Best-Fit + SLUB六项PASS，空闲页31926→31926；Buddy四项及SLUB六项PASS，空闲页31854→31854；接入后make grade仍为20/20。
- 最终组工程保留组员Buddy（0页返回NULL、测试在buddy_pmm.c）；SLUB Best-Fit为31926→31926，Buddy为31925→31925，均六项PASS；组工程官方回归20/20。
- 随后补充page_init可管理上界、页对齐、描述符容量保护，官方回归再次20/20；边界保护后未重跑两种SLUB专项，不冒充已重跑。
- 实际终端输出已写入报告；新增三张截图尚未收到，不虚构图片。
- 鞠昀慧实际使用ChatGPT网页端，历史具体模型未留存，不填写未经核实的Claude Code型号。
- 实际新增的终端错误、修复、重新运行结果，应继续追加，不虚构固定迭代次数。


## 9. 既有任务的四段SPEC整理（不冒充历史逐字记录）

| 任务 | 背景与输入 | 目标与输出 | 实现约束 | 验收证据 |
|---|---|---|---|---|
| page_init | DTB解析结果、end和Page/pmm接口 | 设置偏移、页号、描述符和空闲区 | 范围/溢出/上界/对齐、元数据不得分配，空区不注册 | 原官方Page Init、保留页和地址往返检查 |
| First-Fit分析 | default_pmm.c | 用自己的话解释初始化、申请、释放合并及改进 | 区分链表邻接与物理连续；不声称消除所有碎片 | 对应函数及具体选块例子 |
| Best-Fit | First-Fit框架与官方测试 | 注册最小合格块策略并接入 | 保留测试、允许精确匹配早停、拆分/合并和计数正确 | 原自测、官方20/20、扩展回归 |
| Buddy实现 | pmm接口和单段可用区 | 分阶拆分/伙伴合并 | 非2幂区间、物理对齐、原请求释放约定、禁止部分释放 | 取整、耗尽、唯一性、合并恢复 |
| Buddy测试 | 上述实际接口 | 独立断言检查 | 组版本0页返回NULL；个人版本断言拒绝；按对应接口测试 | 数据结构及外部行为检查，不只打印PASS |
| 双GDB | QEMU4.1.1源码、mmu_probe和内核ELF | 观察S态Sv39查表及软件TLB回填 | 两个地址空间、有效特权、条件断点、PA基址与字节偏移 | satp/idx/PTE/PA/标签与实际读值 |

## 10. 个人双GDB任务的复现prompt整理

```text
我需要在已有ucore Lab2中，用带debug_info的QEMU4.1.1观察实际的地址翻译。
请一次只给一组操作，区分A运行QEMU、B宿主gdb、C内核RISC-V GDB。
先让C停在mmu_probe，取得mmu_probe_value的实际VA；B对该VA的数据load设置条件断点，
跟踪load_helper -> tlb_fill -> riscv_cpu_tlb_fill -> get_physical_address。
逐步解释satp、根表PA、idx、pte_addr、PTE和权限分支，遇到根层大页不要误称必须走三级。
该版本输出physical若为4KiB页基址，应加低12位偏移再与固定映射结果对照。
返回后检查entry->addr_read是否变为目标虚拟页，最后由C检查mmu_probe_result。
遇到未连接目标、宿主暂停导致客户机等待、符号/库源码缺失等情况，按实际输出排查，
不要把mmu_idx=3的固件访问直接说成S态Sv39查表。
不要根据最终截图编造历史迭代；记录真实指令、参数、结果和未完成项目。
```
