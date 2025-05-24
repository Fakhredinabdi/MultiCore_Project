# پروژه: جدول هش هم‌زمان با قفل‌های بخش‌شده (Sharded Concurrent Hash Map)

## مقدمه
این پروژه یک پیاده‌سازی ساده از یک **جدول هش** (Hash Map) را ارائه می‌دهد که از **قفل‌های بخش‌شده** (Sharded Locks) برای افزایش هم‌زمانی و کارایی در محیط‌های چندریسمانی (multi-threaded) بهره می‌برد. هدف این پروژه کاهش ترافیک قفل‌ها و جلوگیری از آشفتگی بیش از حد در دسترسی هم‌زمان به ساختار داده است.

## ساختار پروژه
```text
.
├── src
│   ├── main.c         # تابع اصلی برنامه (خواندن ورودی، راه‌اندازی Map، تولید بار و سنجش زمان)
│   ├── map.h           # تعاریف و دکلرها برای Map
│   └── map.c           # پیاده‌سازی Map با شاردینگ و کنترل برخوردها
├── Makefile            # فایل ساخت (کامپایل) پروژه
└── README.md           # این فایل
```

## شرح الگوریتم و جریان کلی
1. **خواندن ورودی**  
   - تعداد عناصر (`dataSize`)  
   - تعداد ریسمان‌ها (`threads`)  
   - اندازه جدول هش (`tableSize`)

2. **راه‌اندازی Map**  
   - ساخت `shardCount` شارد (معمولاً برابر با تعداد هسته‌های CPU یا مضربی از آن)  
   - هر شارد دارای یک قفل (`pthread_mutex_t`) و یک آرایه از سطل‌ها (buckets) است.

3. **توزیع کلیدها به شاردها**  
   - با استفاده از یک **تابع هش** روی کلید  
   - شاخص شارد = `(hash(key) % shardCount)`

4. **ورود (insert)**  
   - محاسبه شارد مربوط  
   - قفل کردن آن شارد  
   - درج در لیست پیوندی (chaining) در داخل آن سطل  
   - شمارش برخوردها (در صورت وجود برخورد اندیس)  
   - آزادسازی قفل

5. **گزارش‌گیری**  
   - در پایان، زمان اجرای کل و تعداد برخوردهای ثبت‌شده از طریق شمارنده‌های اتمیک نمایش داده می‌شود.

## ساختار داده و پیاده‌سازی شاردینگ
در `map.h` داریم:
```c
typedef struct MapEntry {
    const char *key;
    int value;
    struct MapEntry *next;
} MapEntry;

typedef struct {
    pthread_mutex_t lock;
    MapEntry **buckets;    // آرایه نشانی به لیست‌های پیوندی
} MapShard;

typedef struct {
    size_t shardCount;
    size_t bucketCountPerShard;
    MapShard *shards;
    atomic_ulong collisionCount;
} ConcurrentHashMap;
```

- **شارد** (MapShard):  
  - `lock`: قفل مختص به این بخش  
  - `buckets`: آرایه‌ای از نشانگر به لیست‌های پیوندی برای مدیریت برخوردها  

- **ConcurrentHashMap**:  
  - `shardCount`: تعداد شاردها  
  - `bucketCountPerShard`: تعداد سطل (bucket) در هر شارد  
  - `shards`: آرایه‌ای از شاردها  
  - `collisionCount`: شمارنده اتمیک برای ثبت تعداد برخوردها

## تابع هش (Hash Function)
برای تولید اندیس‌، از الگوریتم **FNV-1a** استفاده شده است:

$$ \begin{aligned} \text{hash} &\leftarrow 1469598103934665603_{10} \quad (\text{offset basis})\\ \text{for each byte } b &: \\ \quad \text{hash} &\leftarrow (\text{hash} \oplus b)\times 1099511628211_{10} \quad (\text{FNV prime}) \end{aligned} $$

- مزایا: ساده، سریع و توزیع مناسب  
- مرجع اصلی:  
  - Glenn Fowler, Landon Curt Noll, and Phong Vo. *FNV Hash.*  
    http://www.isthe.com/chongo/tech/comp/fnv/

> **نکته:** در صورت نیاز به توزیع بهینه‌تر روی داده‌های بزرگ، می‌توان از [MurmurHash3 (Austin Appleby, 2011)](https://arxiv.org/abs/1402.8068) یا سایر توابع هش سریع و قدرتمند استفاده کرد.

## مدیریت برخوردها (Collision Resolution)
- از روش **Chaining** با لیست پیوندی استفاده شده است.  
- در صورت درج زوج کلید-مقدار جدید در یک سطل که قبلاً اشغال شده،  
  - شمارنده برخورد (`collisionCount`) یک واحد افزایش می‌یابد.  
  - عنصر جدید در ابتدای لیست قرار می‌گیرد.

## هم‌زمانی و شاردینگ
- با بخش‌بندی جدول به چند شارد مستقل، ترافیک قفل‌ها کاهش می‌یابد.  
- هر شارد قفل و سطل‌های مجزایی دارد و دسترسی‌های هم‌زمان به شاردهای متفاوت بدون تداخل انجام می‌شود.  
- تنها زمانی قفل یک شارد گرفته می‌شود که یک ریسه قصد درج یا جستجو در آن بخش داشته باشد.

## نحوه‌ی کامپایل و اجرا
1. پیش‌نیاز:  
   - کامپایلر GCC با پشتیبانی از C11  
   - کتابخانه pthread  
2. در شاخه اصلی:
   ```bash
   make
   ```
3. برای اجرا:
   ```bash
   ./main <dataSize> <threads> <tableSize>
   ```
   مثال:
   ```bash
   ./main 1000000 4 131071
   ```

## خروجی و تولید نمودارها
- برنامه دو فایل متنی خروجی تولید می‌کند:  
  - `executions.dat` (زمان اجرا)  
  - `collisions.dat` (تعداد برخوردها)

- برای رسم نمودارها می‌توانید از اسکریپت‌های Python با استفاده از matplotlib بهره ببرید (اسکریپت‌ها در پوشه `plots/`).

## منابع و مراجع
1. Fowler–Noll–Vo Hash Function (FNV-1a):  
   http://www.isthe.com/chongo/tech/comp/fnv/  
2. Appleby, A. (2011). *MurmurHash3.*  
   https://arxiv.org/abs/1402.8068  
3. مستندات POSIX Threads (pthread):  
   https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutex_lock.html

---
امیدواریم این پروژه و مستندات آن برای درک بهتر پیاده‌سازی جداول هش هم‌زمان مفید باشد.  
سوالات و پیشنهادات خود را می‌توانید به بخش Issues در مخزن پروژه ارسال کنید!
