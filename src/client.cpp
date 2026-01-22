#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QVector>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>
#include <QtGui/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

namespace {
constexpr int kMaxRoomLines = 200;
constexpr int kMaxNotifications = 20;
}

class PrivateChatDialog : public QDialog {
  Q_OBJECT

 public:
  explicit PrivateChatDialog(const QString& peer, QWidget* parent = nullptr)
      : QDialog(parent), peer_(peer) {
    setWindowTitle(QStringLiteral("Private chat: %1").arg(peer_));
    setMinimumSize(400, 300);

    auto* layout = new QVBoxLayout(this);
    chat_view_ = new QTextEdit(this);
    chat_view_->setReadOnly(true);

    auto* input_layout = new QHBoxLayout();
    input_ = new QLineEdit(this);
    send_button_ = new QPushButton(QStringLiteral("Send"), this);

    input_layout->addWidget(input_);
    input_layout->addWidget(send_button_);

    layout->addWidget(chat_view_);
    layout->addLayout(input_layout);

    connect(send_button_, &QPushButton::clicked, this, &PrivateChatDialog::sendClicked);
    connect(input_, &QLineEdit::returnPressed, this, &PrivateChatDialog::sendClicked);
  }

  QString peer() const { return peer_; }

  void appendMessage(const QString& line) {
    chat_view_->append(line);
  }

 signals:
  void messageReady(const QString& peer, const QString& message);

 private slots:
  void sendClicked() {
    const QString text = input_->text().trimmed();
    if (text.isEmpty()) {
      return;
    }
    emit messageReady(peer_, text);
    appendMessage(QStringLiteral("you: %1").arg(text));
    input_->clear();
  }

 private:
  QString peer_;
  QTextEdit* chat_view_ = nullptr;
  QLineEdit* input_ = nullptr;
  QPushButton* send_button_ = nullptr;
};

class ChatWindow : public QMainWindow {
  Q_OBJECT

 public:
  ChatWindow(const QString& host, int port, QWidget* parent = nullptr)
      : QMainWindow(parent), host_(host), port_(port) {
    setWindowTitle(QStringLiteral("ChatApp"));
    setMinimumSize(1000, 700);

    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* main_layout = new QHBoxLayout(central);

    main_layout->addWidget(buildControlPanel());
    main_layout->addWidget(buildRoomPanel(), 1);
    main_layout->addWidget(buildChatPanel(), 2);

    buildMenu();

    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::readyRead, this, &ChatWindow::onReadyRead);
    connect(socket_, &QTcpSocket::connected, this, &ChatWindow::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &ChatWindow::onDisconnected);

    socket_->connectToHost(host_, static_cast<quint16>(port_));
  }

 private:
  QWidget* buildControlPanel() {
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);

    auto* profile_group = new QGroupBox(QStringLiteral("Profile"), panel);
    auto* profile_layout = new QFormLayout(profile_group);
    name_input_ = new QLineEdit(profile_group);
    auto* name_button = new QPushButton(QStringLiteral("Set name"), profile_group);
    profile_layout->addRow(QStringLiteral("Nickname"), name_input_);
    profile_layout->addRow(QString(), name_button);

    connect(name_button, &QPushButton::clicked, this, &ChatWindow::setNickname);

    auto* room_group = new QGroupBox(QStringLiteral("Rooms"), panel);
    auto* room_layout = new QFormLayout(room_group);
    room_name_input_ = new QLineEdit(room_group);
    room_pass_input_ = new QLineEdit(room_group);
    room_pass_input_->setEchoMode(QLineEdit::Password);
    auto* create_button = new QPushButton(QStringLiteral("Create"), room_group);
    auto* join_button = new QPushButton(QStringLiteral("Join"), room_group);

    room_layout->addRow(QStringLiteral("Room"), room_name_input_);
    room_layout->addRow(QStringLiteral("Password"), room_pass_input_);
    room_layout->addRow(QString(), create_button);
    room_layout->addRow(QString(), join_button);

    connect(create_button, &QPushButton::clicked, this, &ChatWindow::createRoom);
    connect(join_button, &QPushButton::clicked, this, &ChatWindow::joinSelectedRoom);

    auto* notifications_group = new QGroupBox(QStringLiteral("Notifications"), panel);
    auto* notifications_layout = new QVBoxLayout(notifications_group);
    notifications_list_ = new QListWidget(notifications_group);
    notifications_layout->addWidget(notifications_list_);

    connect(notifications_list_, &QListWidget::itemDoubleClicked, this,
            &ChatWindow::openNotificationChat);

    auto* test_group = new QGroupBox(QStringLiteral("Test room"), panel);
    auto* test_layout = new QFormLayout(test_group);
    test_room_input_ = new QLineEdit(test_group);
    test_room_input_->setPlaceholderText(QStringLiteral("test-room"));
    bot_count_input_ = new QSpinBox(test_group);
    bot_count_input_->setRange(1, 50);
    bot_count_input_->setValue(5);
    test_start_button_ = new QPushButton(QStringLiteral("Start"), test_group);
    test_stop_button_ = new QPushButton(QStringLiteral("Stop"), test_group);
    test_stop_button_->setEnabled(false);

    auto* test_buttons = new QHBoxLayout();
    test_buttons->addWidget(test_start_button_);
    test_buttons->addWidget(test_stop_button_);

    test_layout->addRow(QStringLiteral("Room"), test_room_input_);
    test_layout->addRow(QStringLiteral("Bots"), bot_count_input_);
    test_layout->addRow(test_buttons);

    connect(test_start_button_, &QPushButton::clicked, this, &ChatWindow::startTestRoom);
    connect(test_stop_button_, &QPushButton::clicked, this, &ChatWindow::stopTestRoom);

    layout->addWidget(profile_group);
    layout->addWidget(room_group);
    layout->addWidget(test_group);
    layout->addWidget(notifications_group, 1);
    layout->addStretch();
    return panel;
  }

  QWidget* buildRoomPanel() {
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);

    auto* label = new QLabel(QStringLiteral("Rooms (center list)"), panel);
    label->setAlignment(Qt::AlignCenter);

    room_list_ = new QListWidget(panel);
    room_list_->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(room_list_, &QListWidget::itemDoubleClicked, this, &ChatWindow::joinRoomFromItem);

    layout->addWidget(label);
    layout->addWidget(room_list_, 1);
    return panel;
  }

  QWidget* buildChatPanel() {
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);

    current_room_label_ = new QLabel(QStringLiteral("Current room: Lobby"), panel);
    room_chat_view_ = new QTextEdit(panel);
    room_chat_view_->setReadOnly(true);

    auto* input_layout = new QHBoxLayout();
    message_input_ = new QLineEdit(panel);
    send_button_ = new QPushButton(QStringLiteral("Send"), panel);

    input_layout->addWidget(message_input_, 1);
    input_layout->addWidget(send_button_);

    connect(send_button_, &QPushButton::clicked, this, &ChatWindow::sendRoomMessage);
    connect(message_input_, &QLineEdit::returnPressed, this, &ChatWindow::sendRoomMessage);

    layout->addWidget(current_room_label_);
    layout->addWidget(room_chat_view_, 1);
    layout->addLayout(input_layout);

    return panel;
  }

  void buildMenu() {
    auto* private_menu = menuBar()->addMenu(QStringLiteral("Private chats"));
    auto* open_action = new QAction(QStringLiteral("Open chat..."), this);
    private_menu->addAction(open_action);
    connect(open_action, &QAction::triggered, this, &ChatWindow::openPrivateChatPrompt);
  }

  void appendRoomLine(const QString& line) {
    room_chat_view_->append(line);
    auto* doc = room_chat_view_->document();
    if (doc->blockCount() > kMaxRoomLines) {
      QTextCursor cursor(doc);
      cursor.movePosition(QTextCursor::Start);
      cursor.select(QTextCursor::BlockUnderCursor);
      cursor.removeSelectedText();
      cursor.deleteChar();
    }
  }

  void addNotification(const QString& sender, const QString& message) {
    auto* item = new QListWidgetItem(
        QStringLiteral("%1: %2").arg(sender, message.left(80)), notifications_list_);
    item->setData(Qt::UserRole, sender);
    notifications_list_->insertItem(0, item);
    while (notifications_list_->count() > kMaxNotifications) {
      delete notifications_list_->takeItem(notifications_list_->count() - 1);
    }
  }

  void openPrivateChat(const QString& user) {
    if (user.isEmpty()) {
      return;
    }
    auto* dialog = private_chats_.value(user, nullptr);
    if (!dialog) {
      dialog = new PrivateChatDialog(user, this);
      connect(dialog, &PrivateChatDialog::messageReady, this, &ChatWindow::sendPrivateMessage);
      private_chats_.insert(user, dialog);
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
  }

  void updateRooms(const QString& payload) {
    room_list_->clear();
    const QStringList tokens = payload.split('|', Qt::SkipEmptyParts);
    for (int i = 0; i + 1 < tokens.size(); i += 2) {
      const QString name = tokens.at(i);
      const QString status = tokens.at(i + 1);
      const bool locked = status == QStringLiteral("locked");
      auto* item = new QListWidgetItem(
          QStringLiteral("%1%2").arg(name, locked ? QStringLiteral(" (locked)") : QString()),
          room_list_);
      item->setData(Qt::UserRole, name);
      item->setData(Qt::UserRole + 1, locked);
    }
  }

  void handlePrivateMessage(const QString& line) {
    QString payload = line.mid(QStringLiteral("[private]").size()).trimmed();
    const int colon_index = payload.indexOf(":");
    if (colon_index <= 0) {
      appendRoomLine(line);
      return;
    }
    const QString sender = payload.left(colon_index).trimmed();
    const QString message = payload.mid(colon_index + 1).trimmed();

    auto* dialog = private_chats_.value(sender, nullptr);
    if (dialog) {
      dialog->appendMessage(QStringLiteral("%1: %2").arg(sender, message));
    }

    addNotification(sender, message);
  }

  void sendLine(const QString& line) {
    if (socket_->state() != QAbstractSocket::ConnectedState) {
      QMessageBox::warning(this, QStringLiteral("Connection"),
                           QStringLiteral("Not connected to server."));
      return;
    }
    const QByteArray data = (line + "\n").toUtf8();
    socket_->write(data);
  }

 private slots:
  void onConnected() {
    appendRoomLine(
        QStringLiteral("Connected to %1:%2.").arg(host_).arg(port_));
    sendLine(QStringLiteral("/rooms"));
  }

  void onDisconnected() {
    appendRoomLine(QStringLiteral("Disconnected from server."));
    stopTestRoom();
  }

  void onReadyRead() {
    buffer_.append(socket_->readAll());
    while (true) {
      int newline_index = buffer_.indexOf('\n');
      if (newline_index < 0) {
        break;
      }
      const QByteArray line_data = buffer_.left(newline_index);
      buffer_.remove(0, newline_index + 1);
      const QString line = QString::fromUtf8(line_data).trimmed();
      if (line.isEmpty()) {
        continue;
      }
      if (line.startsWith(QStringLiteral("ROOMS|"))) {
        updateRooms(line.mid(6));
        continue;
      }
      if (line.startsWith(QStringLiteral("ROOM|"))) {
        const QString room = line.mid(5).trimmed();
        current_room_label_->setText(QStringLiteral("Current room: %1").arg(room));
        continue;
      }
      if (line.startsWith(QStringLiteral("[private]"))) {
        handlePrivateMessage(line);
        continue;
      }
      appendRoomLine(line);
    }
  }

  void sendRoomMessage() {
    const QString message = message_input_->text().trimmed();
    if (message.isEmpty()) {
      return;
    }
    sendLine(message);
    message_input_->clear();
  }

  void setNickname() {
    const QString name = name_input_->text().trimmed();
    if (name.isEmpty()) {
      return;
    }
    sendLine(QStringLiteral("/name %1").arg(name));
  }

  void createRoom() {
    const QString name = room_name_input_->text().trimmed();
    const QString pass = room_pass_input_->text().trimmed();
    if (name.isEmpty()) {
      QMessageBox::information(this, QStringLiteral("Rooms"),
                               QStringLiteral("Room name is required."));
      return;
    }
    QString command = QStringLiteral("/create %1").arg(name);
    if (!pass.isEmpty()) {
      command += QStringLiteral(" %1").arg(pass);
    }
    sendLine(command);
  }

  void joinSelectedRoom() {
    QListWidgetItem* item = room_list_->currentItem();
    if (!item) {
      QMessageBox::information(this, QStringLiteral("Rooms"),
                               QStringLiteral("Select a room from the list."));
      return;
    }
    joinRoomFromItem(item);
  }

  void joinRoomFromItem(QListWidgetItem* item) {
    const QString name = item->data(Qt::UserRole).toString();
    const bool locked = item->data(Qt::UserRole + 1).toBool();
    QString pass = room_pass_input_->text().trimmed();
    if (locked && pass.isEmpty()) {
      pass = QInputDialog::getText(this,
                                   QStringLiteral("Room password"),
                                   QStringLiteral("Enter password for %1").arg(name),
                                   QLineEdit::Password);
    }
    QString command = QStringLiteral("/join %1").arg(name);
    if (!pass.isEmpty()) {
      command += QStringLiteral(" %1").arg(pass);
    }
    sendLine(command);
  }

  void openNotificationChat(QListWidgetItem* item) {
    if (!item) {
      return;
    }
    const QString sender = item->data(Qt::UserRole).toString();
    delete notifications_list_->takeItem(notifications_list_->row(item));
    openPrivateChat(sender);
  }

  void openPrivateChatPrompt() {
    bool ok = false;
    const QString user = QInputDialog::getText(
        this, QStringLiteral("Private chat"), QStringLiteral("User name"), QLineEdit::Normal,
        QString(), &ok);
    if (ok && !user.trimmed().isEmpty()) {
      openPrivateChat(user.trimmed());
    }
  }

  void sendPrivateMessage(const QString& peer, const QString& message) {
    sendLine(QStringLiteral("/msg %1 %2").arg(peer, message));
  }

  void startTestRoom() {
    if (socket_->state() != QAbstractSocket::ConnectedState) {
      QMessageBox::warning(this, QStringLiteral("Connection"),
                           QStringLiteral("Not connected to server."));
      return;
    }
    if (test_active_) {
      return;
    }
    QString room = test_room_input_->text().trimmed();
    if (room.isEmpty()) {
      room = QStringLiteral("test-room");
      test_room_input_->setText(room);
    }

    test_active_ = true;
    test_start_button_->setEnabled(false);
    test_stop_button_->setEnabled(true);

    sendLine(QStringLiteral("/create %1").arg(room));
    sendLine(QStringLiteral("/join %1").arg(room));

    const int bot_count = bot_count_input_->value();
    for (int i = 1; i <= bot_count; ++i) {
      createBot(room, i);
    }
  }

  void stopTestRoom() {
    if (!test_active_ && bot_sockets_.isEmpty()) {
      return;
    }
    test_active_ = false;
    test_start_button_->setEnabled(true);
    test_stop_button_->setEnabled(false);

    for (auto* bot : bot_sockets_) {
      if (bot) {
        bot->disconnectFromHost();
        bot->deleteLater();
      }
    }
    bot_sockets_.clear();
  }

  void createBot(const QString& room, int index) {
    auto* bot = new QTcpSocket(this);
    bot_sockets_.append(bot);
    const QString bot_name = QStringLiteral("Bot%1").arg(index);

    connect(bot, &QTcpSocket::connected, this, [this, bot, bot_name, room]() {
      sendBotLine(bot, QStringLiteral("/name %1").arg(bot_name));
      sendBotLine(bot, QStringLiteral("/join %1").arg(room));
    });
    connect(bot, &QTcpSocket::disconnected, this, [this, bot]() {
      bot_sockets_.removeAll(bot);
      bot->deleteLater();
    });

    bot->connectToHost(host_, static_cast<quint16>(port_));
  }

  void sendBotLine(QTcpSocket* bot, const QString& line) {
    if (!bot || bot->state() != QAbstractSocket::ConnectedState) {
      return;
    }
    const QByteArray data = (line + "\n").toUtf8();
    bot->write(data);
  }

 private:
  QString host_;
  int port_ = 0;
  QTcpSocket* socket_ = nullptr;
  QByteArray buffer_;

  QLineEdit* name_input_ = nullptr;
  QLineEdit* room_name_input_ = nullptr;
  QLineEdit* room_pass_input_ = nullptr;

  QListWidget* room_list_ = nullptr;
  QListWidget* notifications_list_ = nullptr;
  QTextEdit* room_chat_view_ = nullptr;
  QLabel* current_room_label_ = nullptr;
  QLineEdit* message_input_ = nullptr;
  QPushButton* send_button_ = nullptr;
  QLineEdit* test_room_input_ = nullptr;
  QSpinBox* bot_count_input_ = nullptr;
  QPushButton* test_start_button_ = nullptr;
  QPushButton* test_stop_button_ = nullptr;
  QVector<QTcpSocket*> bot_sockets_;
  bool test_active_ = false;

  QMap<QString, PrivateChatDialog*> private_chats_;
};

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  QString host = QStringLiteral("127.0.0.1");
  int port = 5555;

  if (argc >= 2) {
    host = QString::fromLocal8Bit(argv[1]);
  }
  if (argc >= 3) {
    port = QString::fromLocal8Bit(argv[2]).toInt();
  }

  ChatWindow window(host, port);
  window.show();

  return app.exec();
}

#include "client.moc"
