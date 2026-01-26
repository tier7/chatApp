#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QRandomGenerator>
#include <QtCore/QTimer>
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
constexpr int kMaksLiniiPokoju = 200;
constexpr int kMaksPowiadomien = 20;
}

class PrywatnyCzatDialog : public QDialog {
  Q_OBJECT

 public:
  explicit PrywatnyCzatDialog(const QString& rozmowca, QWidget* rodzic = nullptr)
      : QDialog(rodzic), rozmowca_(rozmowca) {
    setWindowTitle(QStringLiteral("Prywatny czat: %1").arg(rozmowca_));
    setMinimumSize(400, 300);

    auto* uklad = new QVBoxLayout(this);
    widok_czatu_ = new QTextEdit(this);
    widok_czatu_->setReadOnly(true);

    auto* uklad_wejscia = new QHBoxLayout();
    pole_wejscia_ = new QLineEdit(this);
    przycisk_wyslij_ = new QPushButton(QStringLiteral("Wyślij"), this);

    uklad_wejscia->addWidget(pole_wejscia_);
    uklad_wejscia->addWidget(przycisk_wyslij_);

    uklad->addWidget(widok_czatu_);
    uklad->addLayout(uklad_wejscia);

    connect(przycisk_wyslij_, &QPushButton::clicked, this, &PrywatnyCzatDialog::klikniecieWyslij);
    connect(pole_wejscia_, &QLineEdit::returnPressed, this,
            &PrywatnyCzatDialog::klikniecieWyslij);
  }

  QString rozmowca() const { return rozmowca_; }

  void dodajWiadomosc(const QString& linia) {
    widok_czatu_->append(linia);
  }

 signals:
  void wiadomoscGotowa(const QString& rozmowca, const QString& wiadomosc);

 private slots:
  void klikniecieWyslij() {
    const QString tekst = pole_wejscia_->text().trimmed();
    if (tekst.isEmpty()) {
      return;
    }
    emit wiadomoscGotowa(rozmowca_, tekst);
    dodajWiadomosc(QStringLiteral("ty: %1").arg(tekst));
    pole_wejscia_->clear();
  }

 private:
  QString rozmowca_;
  QTextEdit* widok_czatu_ = nullptr;
  QLineEdit* pole_wejscia_ = nullptr;
  QPushButton* przycisk_wyslij_ = nullptr;
};

class OknoCzatu : public QMainWindow {
  Q_OBJECT

 public:
  OknoCzatu(const QString& adres_hosta, int port, QWidget* rodzic = nullptr)
      : QMainWindow(rodzic), adres_hosta_(adres_hosta), port_(port) {
    setWindowTitle(QStringLiteral("ChatApp"));
    setMinimumSize(1000, 700);

    auto* centralny = new QWidget(this);
    setCentralWidget(centralny);

    auto* glowny_uklad = new QHBoxLayout(centralny);

    glowny_uklad->addWidget(zbudujPanelSterowania());
    glowny_uklad->addWidget(zbudujPanelPokoi(), 1);
    glowny_uklad->addWidget(zbudujPanelCzatu(), 2);

    zbudujMenu();

    gniazdo_ = new QTcpSocket(this);
    connect(gniazdo_, &QTcpSocket::readyRead, this, &OknoCzatu::poOdczycie);
    connect(gniazdo_, &QTcpSocket::connected, this, &OknoCzatu::poPolaczeniu);
    connect(gniazdo_, &QTcpSocket::disconnected, this, &OknoCzatu::poRozlaczeniu);

    timer_ladowania_ = new QTimer(this);
    connect(timer_ladowania_, &QTimer::timeout, this, &OknoCzatu::wyslijWiadomoscLadujaca);

    gniazdo_->connectToHost(adres_hosta_, static_cast<quint16>(port_));
  }

 private:
  QWidget* zbudujPanelSterowania() {
    auto* panel = new QWidget(this);
    auto* uklad = new QVBoxLayout(panel);

    auto* grupa_profilu = new QGroupBox(QStringLiteral("Profil"), panel);
    auto* uklad_profilu = new QFormLayout(grupa_profilu);
    pole_nazwy_ = new QLineEdit(grupa_profilu);
    auto* przycisk_nazwy = new QPushButton(QStringLiteral("Ustaw nazwę"), grupa_profilu);
    uklad_profilu->addRow(QStringLiteral("Nick"), pole_nazwy_);
    uklad_profilu->addRow(QString(), przycisk_nazwy);

    connect(przycisk_nazwy, &QPushButton::clicked, this, &OknoCzatu::ustawNick);

    auto* grupa_pokoi = new QGroupBox(QStringLiteral("Pokoje"), panel);
    auto* uklad_pokoi = new QFormLayout(grupa_pokoi);
    pole_nazwy_pokoju_ = new QLineEdit(grupa_pokoi);
    pole_hasla_pokoju_ = new QLineEdit(grupa_pokoi);
    pole_hasla_pokoju_->setEchoMode(QLineEdit::Password);
    auto* przycisk_utworz = new QPushButton(QStringLiteral("Utwórz"), grupa_pokoi);
    auto* przycisk_dolacz = new QPushButton(QStringLiteral("Dołącz"), grupa_pokoi);

    uklad_pokoi->addRow(QStringLiteral("Pokój"), pole_nazwy_pokoju_);
    uklad_pokoi->addRow(QStringLiteral("Hasło"), pole_hasla_pokoju_);
    uklad_pokoi->addRow(QString(), przycisk_utworz);
    uklad_pokoi->addRow(QString(), przycisk_dolacz);

    connect(przycisk_utworz, &QPushButton::clicked, this, &OknoCzatu::utworzPokoj);
    connect(przycisk_dolacz, &QPushButton::clicked, this, &OknoCzatu::dolaczDoWybranegoPokoju);

    auto* grupa_powiadomien = new QGroupBox(QStringLiteral("Powiadomienia"), panel);
    auto* uklad_powiadomien = new QVBoxLayout(grupa_powiadomien);
    lista_powiadomien_ = new QListWidget(grupa_powiadomien);
    uklad_powiadomien->addWidget(lista_powiadomien_);

    connect(lista_powiadomien_, &QListWidget::itemDoubleClicked, this,
            &OknoCzatu::otworzPowiadomienieCzat);

    auto* grupa_testowa = new QGroupBox(QStringLiteral("Pokój testowy"), panel);
    auto* uklad_testowy = new QFormLayout(grupa_testowa);
    pole_pokoju_testowego_ = new QLineEdit(grupa_testowa);
    pole_pokoju_testowego_->setPlaceholderText(QStringLiteral("test-room"));
    pole_liczby_botow_ = new QSpinBox(grupa_testowa);
    pole_liczby_botow_->setRange(1, 50);
    pole_liczby_botow_->setValue(5);
    pole_opoznienia_min_ = new QSpinBox(grupa_testowa);
    pole_opoznienia_min_->setRange(100, 10000);
    pole_opoznienia_min_->setValue(1000);
    pole_opoznienia_min_->setSuffix(QStringLiteral(" ms"));
    pole_opoznienia_max_ = new QSpinBox(grupa_testowa);
    pole_opoznienia_max_->setRange(100, 10000);
    pole_opoznienia_max_->setValue(2500);
    pole_opoznienia_max_->setSuffix(QStringLiteral(" ms"));
    przycisk_start_testu_ = new QPushButton(QStringLiteral("Start"), grupa_testowa);
    przycisk_stop_testu_ = new QPushButton(QStringLiteral("Stop"), grupa_testowa);
    przycisk_stop_testu_->setEnabled(false);

    auto* przyciski_testu = new QHBoxLayout();
    przyciski_testu->addWidget(przycisk_start_testu_);
    przyciski_testu->addWidget(przycisk_stop_testu_);

    uklad_testowy->addRow(QStringLiteral("Pokój"), pole_pokoju_testowego_);
    uklad_testowy->addRow(QStringLiteral("Boty"), pole_liczby_botow_);
    uklad_testowy->addRow(QStringLiteral("Opóźnienie bota min"), pole_opoznienia_min_);
    uklad_testowy->addRow(QStringLiteral("Opóźnienie bota max"), pole_opoznienia_max_);
    uklad_testowy->addRow(przyciski_testu);

    connect(przycisk_start_testu_, &QPushButton::clicked, this, &OknoCzatu::uruchomPokojTestowy);
    connect(przycisk_stop_testu_, &QPushButton::clicked, this, &OknoCzatu::zatrzymajPokojTestowy);

    uklad->addWidget(grupa_profilu);
    uklad->addWidget(grupa_pokoi);
    uklad->addWidget(grupa_testowa);
    uklad->addWidget(grupa_powiadomien, 1);
    uklad->addStretch();
    return panel;
  }

  QWidget* zbudujPanelPokoi() {
    auto* panel = new QWidget(this);
    auto* uklad = new QVBoxLayout(panel);

    auto* etykieta = new QLabel(QStringLiteral("Pokoje (lista w środku)"), panel);
    etykieta->setAlignment(Qt::AlignCenter);

    lista_pokoi_ = new QListWidget(panel);
    lista_pokoi_->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(lista_pokoi_, &QListWidget::itemDoubleClicked, this,
            &OknoCzatu::dolaczDoPokojuZElementu);

    uklad->addWidget(etykieta);
    uklad->addWidget(lista_pokoi_, 1);
    return panel;
  }

  QWidget* zbudujPanelCzatu() {
    auto* panel = new QWidget(this);
    auto* uklad = new QVBoxLayout(panel);

    etykieta_aktualnego_pokoju_ = new QLabel(QStringLiteral("Aktualny pokój: Lobby"), panel);
    widok_czatu_pokoju_ = new QTextEdit(panel);
    widok_czatu_pokoju_->setReadOnly(true);

    auto* uklad_wejscia = new QHBoxLayout();
    pole_wiadomosci_ = new QLineEdit(panel);
    przycisk_wyslij_ = new QPushButton(QStringLiteral("Wyślij"), panel);

    uklad_wejscia->addWidget(pole_wiadomosci_, 1);
    uklad_wejscia->addWidget(przycisk_wyslij_);

    connect(przycisk_wyslij_, &QPushButton::clicked, this, &OknoCzatu::wyslijWiadomoscPokoju);
    connect(pole_wiadomosci_, &QLineEdit::returnPressed, this,
            &OknoCzatu::wyslijWiadomoscPokoju);

    uklad->addWidget(etykieta_aktualnego_pokoju_);
    uklad->addWidget(widok_czatu_pokoju_, 1);
    uklad->addLayout(uklad_wejscia);

    return panel;
  }

  void zbudujMenu() {
    auto* menu_prywatne = menuBar()->addMenu(QStringLiteral("Prywatne czaty"));
    auto* akcja_otworz = new QAction(QStringLiteral("Otwórz czat..."), this);
    menu_prywatne->addAction(akcja_otworz);
    connect(akcja_otworz, &QAction::triggered, this, &OknoCzatu::zapytajPrywatnyCzat);
  }

  void dodajLiniePokoju(const QString& linia) {
    widok_czatu_pokoju_->append(linia);
    auto* dokument = widok_czatu_pokoju_->document();
    if (dokument->blockCount() > kMaksLiniiPokoju) {
      QTextCursor kursor(dokument);
      kursor.movePosition(QTextCursor::Start);
      kursor.select(QTextCursor::BlockUnderCursor);
      kursor.removeSelectedText();
      kursor.deleteChar();
    }
  }

  void dodajPowiadomienie(const QString& nadawca, const QString& wiadomosc) {
    auto* element = new QListWidgetItem(
        QStringLiteral("%1: %2").arg(nadawca, wiadomosc.left(80)), lista_powiadomien_);
    element->setData(Qt::UserRole, nadawca);
    lista_powiadomien_->insertItem(0, element);
    while (lista_powiadomien_->count() > kMaksPowiadomien) {
      delete lista_powiadomien_->takeItem(lista_powiadomien_->count() - 1);
    }
  }

  void otworzPrywatnyCzat(const QString& uzytkownik) {
    if (uzytkownik.isEmpty()) {
      return;
    }
    auto* dialog = prywatne_czaty_.value(uzytkownik, nullptr);
    if (!dialog) {
      dialog = new PrywatnyCzatDialog(uzytkownik, this);
      connect(dialog, &PrywatnyCzatDialog::wiadomoscGotowa, this,
              &OknoCzatu::wyslijPrywatnaWiadomosc);
      prywatne_czaty_.insert(uzytkownik, dialog);
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
  }

  void aktualizujPokoje(const QString& ladunek) {
    lista_pokoi_->clear();
    const QStringList tokeny = ladunek.split('|', Qt::SkipEmptyParts);
    for (int i = 0; i + 1 < tokeny.size(); i += 2) {
      const QString nazwa = tokeny.at(i);
      const QString status = tokeny.at(i + 1);
      const bool zablokowany = status == QStringLiteral("locked");
      auto* element = new QListWidgetItem(
          QStringLiteral("%1%2").arg(nazwa, zablokowany ? QStringLiteral(" (locked)") : QString()),
          lista_pokoi_);
      element->setData(Qt::UserRole, nazwa);
      element->setData(Qt::UserRole + 1, zablokowany);
    }
  }

  void obsluzPrywatnaWiadomosc(const QString& linia) {
    QString ladunek = linia.mid(QStringLiteral("[private]").size()).trimmed();
    const int indeks_dwukropka = ladunek.indexOf(":");
    if (indeks_dwukropka <= 0) {
      dodajLiniePokoju(linia);
      return;
    }
    const QString nadawca = ladunek.left(indeks_dwukropka).trimmed();
    const QString wiadomosc = ladunek.mid(indeks_dwukropka + 1).trimmed();

    auto* dialog = prywatne_czaty_.value(nadawca, nullptr);
    if (dialog) {
      dialog->dodajWiadomosc(QStringLiteral("%1: %2").arg(nadawca, wiadomosc));
    }

    dodajPowiadomienie(nadawca, wiadomosc);
  }

  void wyslijLinie(const QString& linia) {
    if (gniazdo_->state() != QAbstractSocket::ConnectedState) {
      QMessageBox::warning(this, QStringLiteral("Połączenie"),
                           QStringLiteral("Brak połączenia z serwerem."));
      return;
    }
    const QByteArray dane = (linia + "\n").toUtf8();
    gniazdo_->write(dane);
  }

 private slots:
  void poPolaczeniu() {
    dodajLiniePokoju(
        QStringLiteral("Połączono z %1:%2.").arg(adres_hosta_).arg(port_));
    wyslijLinie(QStringLiteral("/rooms"));
  }

  void poRozlaczeniu() {
    dodajLiniePokoju(QStringLiteral("Rozłączono z serwerem."));
    zatrzymajPokojTestowy();
  }

  void poOdczycie() {
    bufor_.append(gniazdo_->readAll());
    while (true) {
      int indeks_nowej_linii = bufor_.indexOf('\n');
      if (indeks_nowej_linii < 0) {
        break;
      }
      const QByteArray dane_linii = bufor_.left(indeks_nowej_linii);
      bufor_.remove(0, indeks_nowej_linii + 1);
      const QString linia = QString::fromUtf8(dane_linii).trimmed();
      if (linia.isEmpty()) {
        continue;
      }
      if (linia.startsWith(QStringLiteral("ROOMS|"))) {
        aktualizujPokoje(linia.mid(6));
        continue;
      }
      if (linia.startsWith(QStringLiteral("ROOM|"))) {
        const QString pokoj = linia.mid(5).trimmed();
        etykieta_aktualnego_pokoju_->setText(QStringLiteral("Aktualny pokój: %1").arg(pokoj));
        continue;
      }
      if (linia.startsWith(QStringLiteral("[private]"))) {
        obsluzPrywatnaWiadomosc(linia);
        continue;
      }
      dodajLiniePokoju(linia);
    }
  }

  void wyslijWiadomoscPokoju() {
    const QString wiadomosc = pole_wiadomosci_->text().trimmed();
    if (wiadomosc.isEmpty()) {
      return;
    }
    wyslijLinie(wiadomosc);
    pole_wiadomosci_->clear();
  }

  void ustawNick() {
    const QString nazwa = pole_nazwy_->text().trimmed();
    if (nazwa.isEmpty()) {
      return;
    }
    wyslijLinie(QStringLiteral("/name %1").arg(nazwa));
  }

  void utworzPokoj() {
    const QString nazwa = pole_nazwy_pokoju_->text().trimmed();
    const QString haslo = pole_hasla_pokoju_->text().trimmed();
    if (nazwa.isEmpty()) {
      QMessageBox::information(this, QStringLiteral("Pokoje"),
                               QStringLiteral("Nazwa pokoju jest wymagana."));
      return;
    }
    QString komenda = QStringLiteral("/create %1").arg(nazwa);
    if (!haslo.isEmpty()) {
      komenda += QStringLiteral(" %1").arg(haslo);
    }
    wyslijLinie(komenda);
  }

  void dolaczDoWybranegoPokoju() {
    QListWidgetItem* element = lista_pokoi_->currentItem();
    if (!element) {
      QMessageBox::information(this, QStringLiteral("Pokoje"),
                               QStringLiteral("Wybierz pokój z listy."));
      return;
    }
    dolaczDoPokojuZElementu(element);
  }

  void dolaczDoPokojuZElementu(QListWidgetItem* element) {
    const QString nazwa = element->data(Qt::UserRole).toString();
    const bool zablokowany = element->data(Qt::UserRole + 1).toBool();
    QString haslo = pole_hasla_pokoju_->text().trimmed();
    if (zablokowany && haslo.isEmpty()) {
      haslo = QInputDialog::getText(this,
                                   QStringLiteral("Hasło pokoju"),
                                   QStringLiteral("Podaj hasło do %1").arg(nazwa),
                                   QLineEdit::Password);
    }
    QString komenda = QStringLiteral("/join %1").arg(nazwa);
    if (!haslo.isEmpty()) {
      komenda += QStringLiteral(" %1").arg(haslo);
    }
    wyslijLinie(komenda);
  }

  void otworzPowiadomienieCzat(QListWidgetItem* element) {
    if (!element) {
      return;
    }
    const QString nadawca = element->data(Qt::UserRole).toString();
    delete lista_powiadomien_->takeItem(lista_powiadomien_->row(element));
    otworzPrywatnyCzat(nadawca);
  }

  void zapytajPrywatnyCzat() {
    bool ok = false;
    const QString uzytkownik = QInputDialog::getText(
        this, QStringLiteral("Prywatny czat"), QStringLiteral("Nazwa użytkownika"),
        QLineEdit::Normal, QString(), &ok);
    if (ok && !uzytkownik.trimmed().isEmpty()) {
      otworzPrywatnyCzat(uzytkownik.trimmed());
    }
  }

  void wyslijPrywatnaWiadomosc(const QString& rozmowca, const QString& wiadomosc) {
    wyslijLinie(QStringLiteral("/msg %1 %2").arg(rozmowca, wiadomosc));
  }

  void uruchomPokojTestowy() {
    if (gniazdo_->state() != QAbstractSocket::ConnectedState) {
      QMessageBox::warning(this, QStringLiteral("Połączenie"),
                           QStringLiteral("Brak połączenia z serwerem."));
      return;
    }
    if (test_aktywny_) {
      return;
    }
    QString pokoj = pole_pokoju_testowego_->text().trimmed();
    if (pokoj.isEmpty()) {
      pokoj = QStringLiteral("test-room");
      pole_pokoju_testowego_->setText(pokoj);
    }

    test_aktywny_ = true;
    przycisk_start_testu_->setEnabled(false);
    przycisk_stop_testu_->setEnabled(true);
    ++numer_uruchomienia_botow_;

    wyslijLinie(QStringLiteral("/create %1").arg(pokoj));
    wyslijLinie(QStringLiteral("/join %1").arg(pokoj));

    const int liczba_botow = pole_liczby_botow_->value();
    int opoznienie_min = pole_opoznienia_min_->value();
    int opoznienie_max = pole_opoznienia_max_->value();
    if (opoznienie_min > opoznienie_max) {
      std::swap(opoznienie_min, opoznienie_max);
      pole_opoznienia_min_->setValue(opoznienie_min);
      pole_opoznienia_max_->setValue(opoznienie_max);
    }
    for (int i = 1; i <= liczba_botow; ++i) {
      utworzBota(pokoj, i, opoznienie_min, opoznienie_max);
    }
  }

  void zatrzymajPokojTestowy() {
    if (!test_aktywny_ && gniazda_botow_.isEmpty()) {
      return;
    }
    test_aktywny_ = false;
    przycisk_start_testu_->setEnabled(true);
    przycisk_stop_testu_->setEnabled(false);

    for (auto it = timery_botow_.begin(); it != timery_botow_.end(); ++it) {
      if (it.value()) {
        it.value()->stop();
        it.value()->deleteLater();
      }
    }
    timery_botow_.clear();
    liczniki_wiadomosci_botow_.clear();

    for (auto* bot : gniazda_botow_) {
      if (bot) {
        bot->disconnectFromHost();
        bot->deleteLater();
      }
    }
    gniazda_botow_.clear();
  }

  void utworzBota(const QString& pokoj, int indeks, int opoznienie_min, int opoznienie_max) {
    auto* bot = new QTcpSocket(this);
    gniazda_botow_.append(bot);
    const QString nazwa_bota = QStringLiteral("Bot%1-%2").arg(indeks).arg(numer_uruchomienia_botow_);

    connect(bot, &QTcpSocket::connected, this,
            [this, bot, nazwa_bota, pokoj, opoznienie_min, opoznienie_max]() {
      wyslijLinieBota(bot, QStringLiteral("/name %1").arg(nazwa_bota));
      wyslijLinieBota(bot, QStringLiteral("/join %1").arg(pokoj));
      liczniki_wiadomosci_botow_[bot] = 0;
      auto* timer = new QTimer(bot);
      timer->setSingleShot(true);
      connect(timer, &QTimer::timeout, this,
              [this, bot, nazwa_bota, opoznienie_min, opoznienie_max, timer]() {
        if (!bot || bot->state() != QAbstractSocket::ConnectedState) {
          return;
        }
        const int nastepna_wiadomosc = ++liczniki_wiadomosci_botow_[bot];
        wyslijLinieBota(
            bot,
            QStringLiteral("[%1] wiadomość %2").arg(nazwa_bota).arg(nastepna_wiadomosc));
        if (opoznienie_min > 0 && opoznienie_max >= opoznienie_min) {
          const int opoznienie = QRandomGenerator::global()->bounded(opoznienie_min,
                                                                     opoznienie_max + 1);
          timer->start(opoznienie);
        }
      });
      timery_botow_.insert(bot, timer);
      if (opoznienie_min > 0 && opoznienie_max >= opoznienie_min) {
        const int opoznienie = QRandomGenerator::global()->bounded(opoznienie_min,
                                                                   opoznienie_max + 1);
        timer->start(opoznienie);
      }
    });
    connect(bot, &QTcpSocket::disconnected, this, [this, bot]() {
      if (timery_botow_.contains(bot)) {
        timery_botow_[bot]->stop();
        timery_botow_[bot]->deleteLater();
        timery_botow_.remove(bot);
      }
      liczniki_wiadomosci_botow_.remove(bot);
      gniazda_botow_.removeAll(bot);
      bot->deleteLater();
    });

    bot->connectToHost(adres_hosta_, static_cast<quint16>(port_));
  }

  void wyslijLinieBota(QTcpSocket* bot, const QString& linia) {
    if (!bot || bot->state() != QAbstractSocket::ConnectedState) {
      return;
    }
    const QByteArray dane = (linia + "\n").toUtf8();
    bot->write(dane);
  }

  void wyslijWiadomoscLadujaca() {
    Q_UNUSED(timer_ladowania_);
  }

 private:
  QString adres_hosta_;
  int port_ = 0;
  QTcpSocket* gniazdo_ = nullptr;
  QByteArray bufor_;

  QLineEdit* pole_nazwy_ = nullptr;
  QLineEdit* pole_nazwy_pokoju_ = nullptr;
  QLineEdit* pole_hasla_pokoju_ = nullptr;
  QTimer* timer_ladowania_ = nullptr;

  QListWidget* lista_pokoi_ = nullptr;
  QListWidget* lista_powiadomien_ = nullptr;
  QTextEdit* widok_czatu_pokoju_ = nullptr;
  QLabel* etykieta_aktualnego_pokoju_ = nullptr;
  QLineEdit* pole_wiadomosci_ = nullptr;
  QPushButton* przycisk_wyslij_ = nullptr;
  QLineEdit* pole_pokoju_testowego_ = nullptr;
  QSpinBox* pole_liczby_botow_ = nullptr;
  QSpinBox* pole_opoznienia_min_ = nullptr;
  QSpinBox* pole_opoznienia_max_ = nullptr;
  QPushButton* przycisk_start_testu_ = nullptr;
  QPushButton* przycisk_stop_testu_ = nullptr;
  QVector<QTcpSocket*> gniazda_botow_;
  QHash<QTcpSocket*, QTimer*> timery_botow_;
  QHash<QTcpSocket*, int> liczniki_wiadomosci_botow_;
  bool test_aktywny_ = false;
  int numer_uruchomienia_botow_ = 0;

  QMap<QString, PrywatnyCzatDialog*> prywatne_czaty_;
};

int main(int liczba_argumentow, char* argumenty[]) {
  QApplication aplikacja(liczba_argumentow, argumenty);

  QString adres_hosta = QStringLiteral("127.0.0.1");
  int port = 5555;

  if (liczba_argumentow >= 2) {
    adres_hosta = QString::fromLocal8Bit(argumenty[1]);
  }
  if (liczba_argumentow >= 3) {
    port = QString::fromLocal8Bit(argumenty[2]).toInt();
  }

  OknoCzatu okno(adres_hosta, port);
  okno.show();

  return aplikacja.exec();
}

#include "client.moc"
