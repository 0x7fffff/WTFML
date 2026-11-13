import sys, json, collections; sys.path.insert(0, '_tmp')
import ed

ed.sub('tests/core_modules/Support.hpp', '''void runResourceDetailTests();''',
'''void runResourceDetailTests();

// =========================================================================
// DQ 节：主页「每日一言」的判据层（`src/util/DailyQuote`）—— 语种与限长、按日期
// 定选题、三种接口的解析、当天缓存、句子池、36 条兜底、署名。全脱网：喂的都是写死的字节。
// =========================================================================

void runDailyQuoteTests();''')

ed.sub('tests/core_modules/resources_main.cpp', '''    runResourceDetailTests();''',
'''    runResourceDetailTests();
    runDailyQuoteTests();''')

ed.sub('tests/core_modules.cmake', ''' core_modules/ResourceDetailTests.cpp''',
''' core_modules/ResourceDetailTests.cpp
 # DQ：主页「每日一言」的判据层（纯函数 + 假响应，不联网）。
 core_modules/DailyQuoteTests.cpp''')

p = 'tests/core_modules/owners.json'
data = json.load(open(p, encoding='utf-8'), object_pairs_hook=collections.OrderedDict)
data['DailyQuoteTests.cpp'] = 'resources'
open(p, 'w', encoding='utf-8', newline='\n').write(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
print('owners ok')
