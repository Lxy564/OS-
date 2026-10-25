# prompt.md

> 本文件汇总 Lab2 所有迭代使用的提示词，按模块顺序整理，和 report.md 一一对应

## 1. page_init 物理内存初始化提示词

【ucore RISC‑V Lab2 编码任务】
文件：kern/mm/pmm.c 待实现函数：static void page_init (void); 调用者：pmm_init () 接口说明：无入参，无返回值。

功能需求： 读取设备树解析得到的物理内存信息，构建物理页描述符布局； 排除 OpenSBI、内核镜像、Page 描述符数组所占用的页面； 将剩余、页对齐后的完整空闲页面交给当前 pmm_manager 管理，完成空闲区注册。 函数正常返回时，必须正确赋值全局变量 va_pa_offset、npage、pages；使用 cprintf 输出探测到的物理内存范围； 若设备树内存信息非法，调用 panic 终止初始化； 如果对齐之后不存在合法的完整空闲页面，绝对不能调用 init_memmap ()。

实现约束：
1. 严格区分虚拟地址、物理地址、物理页号、Page 描述符地址，禁止地址类型混用；
2. 空闲区起始地址向上页对齐，DRAM 内存上界向下页对齐；
3. 内核镜像、Page 描述符数组所在页面不加入空闲链表；
4. 所有用到的变量必须显式定义，禁止出现未定义变量（禁止 nbase 这类未声明变量）。

允许使用的资源： get_memory_base ()、get_memory_size ()、panic ()、cprintf ()、init_memmap ()、pa2page () 链接符号 end []，结构体 struct Page； 框架提供宏：PGSIZE、ROUNDUP、ROUNDDOWN、PADDR、KADDR、SetPageReserved、set_page_ref。

输出要求： 仅输出完整的 page_init 函数代码，ucore 原版风格，附带简短注释，不修改文件中其他已有代码，代码可以直接编译通过。

## 2. Best‑Fit 分配器提示词

【ucore RISC‑V Lab2 编码任务：Best‑Fit 物理内存分配器】
文件：kern/mm/best_fit_pmm.c 注意：该文件已经存在，只填充 //lab2 function2‑2 YOUR CODE 标记之间的代码，不要新建文件、不要修改 basic_check () 和 best_fit_check () 这两个测试函数。

背景：参考 default_pmm.c 的 First‑Fit 框架实现 Best‑Fit。 需要自己定义内部全局空闲链表结构、相关管理函数，并且定义 const struct pmm_manager best_fit_pmm_manager 完成注册。
struct pmm_manager 成员：
struct pmm_manager {
const char *name;
void (*init)(void);
void (*init_memmap)(struct Page *base, size_t n);
struct Page *(*alloc_pages)(size_t n);
void (*free_pages)(struct Page *base, size_t n);
size_t (*nr_free_pages)(void);
};
Best‑Fit 规则：分配时遍历空闲链表，挑选满足 n 页需求、块大小最小的空闲块；分割块，剩余部分放回链表；释放时做相邻空闲块合并。

可用：struct Page、list.h 链表接口、PGSIZE、PageReserved、SetPageReserved、set_page_ref、list_init/list_add/list_del 等。
输出：只输出 YOUR CODE 区域内需要填入的代码，ucore 风格简短注释，不改动文件其他内容。

## 3. Buddy System 伙伴系统实现提示词

【ucore RISC‑V Lab2 编码任务：Buddy System 伙伴系统物理内存分配器】
文件：kern/mm/buddy_pmm.c 配套头文件：kern/mm/buddy_pmm.h

背景：参考 default_pmm.c/best_fit_pmm.c 的 ucore 物理内存管理框架，从零实现 Buddy System（伙伴系统），无预先提供的代码模板与测试函数。
要求适配 pmm.h 中定义的 struct pmm_manager：
struct pmm_manager {
const char *name;
void (*init)(void);
void (*init_memmap)(struct Page *base, size_t n);
struct Page *(*alloc_pages)(size_t n);
void (*free_pages)(struct Page *base, size_t n);
size_t (*nr_free_pages)(void);
};

算法规则：
1. 所有空闲块大小必须是 2 的整数次幂（1,2,4,8,16... 页）；
2. 分配：向上取整，找到能满足 n 页需求的最小 2 的幂大小块；如果块尺寸大于需求，递归对半拆分，直到得到合适大小的块，拆分出来的另一半伙伴放回对应空闲链表；
3. 释放：释放块之后，检查它的伙伴块是否空闲、大小相同；如果满足条件，将二者合并为更大的块，循环合并直到伙伴不可合并；
4. 使用一组空闲链表（桶链表），每一条链表专门存放固定大小（2^k 页）的空闲块。

函数职责：
‑ buddy_init：初始化所有大小桶链表，清空空闲计数
‑ buddy_init_memmap：将一段连续空闲物理页，拆分为合法的 2 的幂块加入对应桶，初始化 Page 属性
‑ buddy_alloc_pages (size_t n)：伙伴系统分配连续 n 页，找不到合适块返回 NULL
‑ buddy_free_pages (struct Page *base, size_t n)：释放连续块，自动伙伴合并
‑ buddy_nr_free_pages：返回当前总的空闲页面数目

可用资源： struct Page、list.h 链表接口、PGSIZE、PageReserved、SetPageReserved、set_page_ref、list_init/list_add/list_del、log2page 等 ucore 原有宏与工具。

输出约定：
1. 输出 buddy_pmm.c 完整实现代码；
2. 同步输出 buddy_pmm.h 的头文件代码，只包含头保护宏、#include <pmm.h>、extern 管理器声明；
3. 风格对齐 default_pmm.c，简短注释；
4. 生成的代码编译后，在 pmm.c 切换 pmm_manager = &buddy_pmm_manager 即可接入内核，可正常和原有 page_init 配套工作。

## 4. Buddy System 测试函数 buddy_check 提示词

【ucore RISC‑V Lab2 编码任务：Buddy System 伙伴系统测试函数生成】
前提：已经基于前面 prompt 生成了 buddy_pmm.c 的伙伴系统分配器，管理器名字：buddy_pmm_manager。
需要编写测试函数 buddy_check ()，放在 buddy_pmm.c 文件内，遵循原有 default /best‑fit 的测试写法风格，使用 assert 断言校验，不修改伙伴系统原有实现代码。

测试需要覆盖的核心场景与边界：
1. 基础分配释放：分配单页、正常释放，分配后页不可重复分配；
2. 向上取整特性：请求非 2 的幂页数（例如请求 3 页，伙伴系统实际分配 4 页）；
3. 拆分逻辑：大块拆分后，剩余伙伴块留在空闲链表；
4. 伙伴合并逻辑：释放一对伙伴块时自动合并为更大块；
5. 链式多次合并：连续释放多层伙伴，逐级向上合并；
6. 越界 / 非法场景：请求 0 页、请求页数超过总空闲页数，应当返回 NULL；
7. 碎片校验：分配释放一轮之后，空闲总页数统计和实际链表统计一致；
8. 连续多块交叉分配释放，模拟随机使用场景。

约束：
‑ 函数名：static void buddy_check (void)
‑ 测试内部可以临时保存、重置空闲链表与 nr_free，避免污染全局内存状态（参考 best_fit 里面 basic_check 写法）；
‑ 测试通过时无断言报错，失败触发 assert；
‑ 输出只输出 buddy_check 函数完整代码，注释说明每一段测试的目的，ucore 原有代码风格。

---

### 使用说明

> 1. 本文件为 Lab2 实验全部原始提示词记录，对应 report.md 中各个模块迭代过程；
> 2. 顺序：page_init → Best‑Fit → Buddy 实现 → Buddy 测试；
> 3. 每个提示词是经过多轮迭代后，最终能够成功编译、通过测试的版本。
