# Быстрые команды для DAP SDK

## 🔥 Самые короткие промпты

### Загрузка контекста (новый чат):
```
загрузи контекст: ./context/scripts/load_full_context.sh + расскажи что делаем
```

### Обновление прогресса:
```
обнови прогресс: [%] + что сделано + что дальше
```

### Анализ проблемы:
```
проблема с [что] - найди и исправь, обнови контекст
```

### Планирование шагов:
```
спланируй следующие 3-5 шагов на основе текущего прогресса
```

### Создание новой задачи:
```
новая задача: [название] в модуле [crypto/core/net] - создай .local/ файлы
```

## ⚡ Однострочники

| Что нужно | Команда |
|-----------|---------|
| Загрузить всё | `./context/scripts/load_full_context.sh` |
| Текущая задача | `jq '.current_task.name' context/.local/index.json` |
| Прогресс | `jq '.overall_progress.percentage' context/.local/progress.json` |
| Следующие шаги | `jq '.next_immediate_steps' context/.local/progress.json` |
| Файлы Chipmunk | `jq '.components.chipmunk.key_files[]' context/modules/crypto.json` |
| Крипто модуль | `./context/scripts/load_module.sh crypto` |

## 🎯 Шаблоны промптов

### Начало работы:
```
контекст + план на сегодня
```

### Завершение работы:
```
обнови прогресс + запиши что сделал + план на завтра
```

### Отладка:
```
баг в [файл/функция] - анализ + исправление + обновление контекста
```

### Рефакторинг:
```
рефактор [компонент] - план + реализация + обновление структуры
```

### Тестирование:
```
тесты для [компонент] - создание + запуск + обновление прогресса
```

## 📝 Быстрые шаблоны

### Milestone:
```json
{
  "id": "название",
  "name": "Описание",
  "progress": 100,
  "status": "completed", 
  "completion_date": "сегодня"
}
```

### Проблема:
```json
{
  "issue": "Что случилось",
  "fix": "Как исправили",
  "files": ["список файлов"],
  "impact": "На что повлияло"
}
```

### Задача:
```json
{
  "id": "task_id",
  "name": "Название",
  "module": "crypto|core|net|other",
  "status": "planned|active",
  "priority": "high|medium|low"
}
```

## 🚀 Алиасы для .bashrc/.zshrc

```bash
# Добавить в ~/.bashrc или ~/.zshrc
alias dap-ctx='./context/scripts/load_full_context.sh'
alias dap-crypto='./context/scripts/load_module.sh crypto'
alias dap-progress='jq ".overall_progress.percentage" context/.local/progress.json'
alias dap-task='jq ".current_task.name" context/.local/index.json'
alias dap-next='jq ".next_immediate_steps" context/.local/progress.json'
```

## 💡 Мнемоники

- **Контекст** = `загрузи контекст`
- **Прогресс** = `обнови прогресс: X%`
- **Проблема** = `проблема с X - исправь`
- **План** = `спланируй шаги`
- **Новая задача** = `новая задача: X`

## ⭐ Самые частые команды

1. `загрузи контекст` - начало работы
2. `обнови прогресс: X%` - отчет о работе  
3. `проблема с X - исправь` - отладка
4. `спланируй шаги` - планирование
5. `что дальше делать?` - когда застрял 