# rksi_chat

Пример простого чат сервера поверх TCP/IP. Пример написан на языке С++. 
Чат отвечает по протоколу Telnet (RFC 854)

<!--
Этот пример был рассмотрен 16 апреля 2022 в рамках курса "Системное программирование". Второй урок.
Это всего лишь шаблон-заготовка. Он имеет проблемы с многопточностью. На следующих занятиях мы попробоуем улучшить его.
-->

Что планируется улучшить:  
1. ~~Защитить от коннекта с браузера - выдвать соообщение что нужно использовать telnet~~
2. ~~Исправить потенциальную проблему с многопоточностью (thread safe)~~
3. ~~Добавить пользовательские команды в чат~~
4. ~~Добавить логи~~
5. Хранить историю последних сообщений, чтобы вновь зашедший пользователь видел предыдущие сообщения
6. Добавить серверную консоль для управления чатом, по крайне мере для корректного останова сервера
7. Добавить шифрование сообщений (но это неточно)
8. Разработать верхнеуровневый протокол для контроля сообщений и адресации (обсудим на лекции)
9. Может быть что-то ещё?
