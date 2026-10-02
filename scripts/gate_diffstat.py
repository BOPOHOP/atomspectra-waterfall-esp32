import sys
import subprocess
from collections import defaultdict

def parse_numstat(text):
    """
    Парсит вывод git diff --numstat.
    Возвращает список кортежей (added, deleted, path, is_binary).
    """
    result = []
    if not text:
        return result
    
    lines = text.strip().split('\n')
    for line in lines:
        if not line:
            continue
        
        parts = line.split('\t')
        if len(parts) < 3:
            continue
            
        added_str, deleted_str = parts[0], parts[1]
        path = '\t'.join(parts[2:]) # Путь может содержать табуляции (редко, но возможно)
        
        is_binary = False
        try:
            added = int(added_str)
            deleted = int(deleted_str)
        except ValueError:
            # Бинарные файлы имеют '-' вместо чисел
            added = 0
            deleted = 0
            is_binary = True
            
        result.append((added, deleted, path, is_binary))
        
    return result

def group_of(path):
    """
    Определяет группу файла по его пути.
    Группы: main, web, scripts, tests, docs, . (корень).
    """
    # Убираем ведущий слэш если есть
    clean_path = path.lstrip('/')
    
    if not clean_path or '/' not in clean_path:
        return '.'
        
    top_dir = clean_path.split('/')[0].lower()
    
    groups = ['main', 'web', 'scripts', 'tests', 'docs']
    
    if top_dir in groups:
        return top_dir
    else:
        # Если верхний каталог не входит в известные группы, 
        # но файл не в корне, логично было бы вернуть сам каталог или '.'
        # По условию "прочее — . для файлов в корне". 
        # Для файлов в других неизвестных директориях условие не уточнено явно, 
        # но обычно diffstat группирует по известным папкам.
        # Интерпретируем "прочее" как всё, что не попало в список и не в корне -> '.'
        return '.'

def main():
    if len(sys.argv) != 4:
        print("Usage: gate_diffstat.py <repo_dir> <base_rev> <head_rev>", file=sys.stderr)
        sys.exit(1)
        
    repo_dir = sys.argv[1]
    base_rev = sys.argv[2]
    head_rev = sys.argv[3]
    
    try:
        proc = subprocess.run(
            ['git', '-C', repo_dir, 'diff', '--numstat', f'{base_rev}..{head_rev}'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True
        )
    except subprocess.CalledProcessError as e:
        print(e.stderr.decode('utf-8', errors='replace'), file=sys.stderr)
        sys.exit(2)
        
    output = proc.stdout.decode('utf-8', errors='replace')
    
    # Настройка кодировки stdout для корректного вывода кириллицы/спецсимволов
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
        
    stats = parse_numstat(output)
    
    # Группировка данных
    groups_data = defaultdict(list)
    
    for added, deleted, path, is_binary in stats:
        group = group_of(path)
        groups_data[group].append((added, deleted, path, is_binary))
        
    total_files = 0
    total_added = 0
    total_deleted = 0
    
    # Сортировка групп для предсказуемого вывода (опционально, но полезно)
    sorted_groups = sorted(groups_data.keys())
    
    for group in sorted_groups:
        items = groups_data[group]
        
        # Сортировка внутри группы по убыванию A+D
        items.sort(key=lambda x: x[0] + x[1], reverse=True)
        
        group_files = len(items)
        group_added = sum(item[0] for item in items)
        group_deleted = sum(item[1] for item in items)
        
        print(f"{group}: файлов {group_files}, +{group_added}, -{group_deleted}")
        
        for added, deleted, path, is_binary in items:
            binary_marker = " (binary)" if is_binary else ""
            print(f"+{added} -{deleted} {path}{binary_marker}")
            
        total_files += group_files
        total_added += group_added
        total_deleted += group_deleted
        
    print(f"ИТОГО: файлов {total_files}, +{total_added}, -{total_deleted}")

if __name__ == '__main__':
    main()
