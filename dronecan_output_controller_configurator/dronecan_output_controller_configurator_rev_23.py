#!/usr/bin/env python3
import json
import sys
import threading
import time
from dataclasses import dataclass

try:
    import dronecan
except ImportError:
    dronecan = None

from PySide6.QtCore import QEvent, QObject, QSettings, QTimer, Qt, Signal
from PySide6.QtGui import QAction, QColor, QCursor, QPalette
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QFileDialog, QHBoxLayout,
    QHeaderView, QLabel, QLineEdit, QMainWindow, QMessageBox, QPushButton,
    QSpinBox, QStatusBar, QTabWidget, QTableWidget, QTableWidgetItem,
    QTextEdit, QToolTip, QVBoxLayout, QWidget
)

APP_TITLE = "DroneCAN Output Controller Configurator"
APP_REVISION = "rev.23"

CONFIG_FILE_FORMAT = "dronecan_output_controller"
CONFIG_FILE_VERSION = 1

LOCAL_NODE_ID = 100
DEFAULT_TARGET_NODE_ID = 42
DEFAULT_CAN_IFACE = "can0"
DEFAULT_CAN_BITRATE = 500000



TOOLTIP_DELAY_MS = 600


class DelayedToolTipManager(QObject):
    """
    Полностью заменяет штатный таймер Qt tooltip.

    При переходе на НОВЫЙ объект:
      1) текущая подсказка сразу скрывается;
      2) запускается новый таймер TOOLTIP_DELAY_MS;
      3) только после выдержки показывается подсказка.

    Поэтому уже открытая подсказка не "перетекает" мгновенно
    с одного поля таблицы на соседнее.
    """

    def __init__(self, app):
        super().__init__(app)
        self.app = app
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.setInterval(TOOLTIP_DELAY_MS)
        self.timer.timeout.connect(self._show_pending)

        self.pending_key = None
        self.pending_text = ""
        self.current_key = None

        # Нужны MouseMove-события даже без нажатых кнопок.
        self._enable_mouse_tracking(app)

    def _enable_mouse_tracking(self, root):
        for widget in root.allWidgets():
            widget.setMouseTracking(True)

    def refresh_widgets(self):
        # Вызывается после построения/изменения динамических частей UI.
        self._enable_mouse_tracking(self.app)

    @staticmethod
    def _tooltip_from_widget(widget):
        w = widget
        while w is not None:
            tip = w.toolTip()
            if tip:
                return ("widget", id(w)), tip
            w = w.parentWidget()
        return None, ""

    def _target_at_cursor(self):
        pos = QCursor.pos()
        widget = QApplication.widgetAt(pos)
        if widget is None:
            return None, ""

        # Для таблицы tooltip может принадлежать QTableWidgetItem,
        # а не самому viewport/cell-widget.
        w = widget
        while w is not None:
            parent = w.parentWidget()
            if isinstance(parent, QTableWidget):
                table = parent
                vp_pos = table.viewport().mapFromGlobal(pos)
                item = table.itemAt(vp_pos)
                if item is not None and item.toolTip():
                    return ("table-item", id(table), item.row(), item.column()), item.toolTip()

                cell = table.cellWidget(
                    table.rowAt(vp_pos.y()),
                    table.columnAt(vp_pos.x())
                )
                if cell is not None and cell.toolTip():
                    return ("table-cell", id(table), table.rowAt(vp_pos.y()), table.columnAt(vp_pos.x())), cell.toolTip()

                # Если у ячейки нет своей подсказки, разрешаем общий tooltip viewport.
                tip = table.viewport().toolTip()
                if tip:
                    return ("table-viewport", id(table)), tip
                break
            w = parent

        return self._tooltip_from_widget(widget)

    def _schedule_for_cursor(self):
        key, text = self._target_at_cursor()

        if key == self.current_key:
            return

        # Новый объект всегда начинает новый цикл задержки.
        self.current_key = key
        self.pending_key = None
        self.pending_text = ""
        self.timer.stop()
        QToolTip.hideText()

        if key is not None and text:
            self.pending_key = key
            self.pending_text = text
            self.timer.start()

    def _show_pending(self):
        if self.pending_key is None or not self.pending_text:
            return

        key, text = self._target_at_cursor()
        if key != self.pending_key or text != self.pending_text:
            return

        QToolTip.showText(QCursor.pos(), self.pending_text)
        self.pending_key = None
        self.pending_text = ""

    def eventFilter(self, obj, event):
        et = event.type()

        # Полностью подавляем штатную механику QToolTip.
        if et == QEvent.ToolTip:
            return True

        if et in (QEvent.MouseMove, QEvent.Enter):
            self._schedule_for_cursor()

        elif et in (QEvent.Leave, QEvent.Hide):
            # Не прячем вслепую при Leave дочернего элемента: курсор мог
            # просто перейти в соседний child. Пересчёт делаем по текущей позиции.
            QTimer.singleShot(0, self._schedule_for_cursor)

        return False


CAN_BITRATES = (125000, 250000, 500000, 1000000)
TYPE_ITEMS = ["DISABLED", "PWM", "ON_OFF", "PULSE"]
TYPE_TO_INT = {name: index for index, name in enumerate(TYPE_ITEMS)}
INT_TO_TYPE = {index: name for index, name in enumerate(TYPE_ITEMS)}
RETRIG_ITEMS = ["IGNORE", "RESTART"]
RETRIG_TO_INT = {name: index for index, name in enumerate(RETRIG_ITEMS)}
INT_TO_RETRIG = {index: name for index, name in enumerate(RETRIG_ITEMS)}

SYSTEM_PARAMS = (
    ("NODE_ID", 1, 125),
    ("CAN_BITRATE", 125000, 1000000),
    ("PCA_COUNT", 1, 2),
    ("FS_TIMEOUT", 10, 60000),
)

OUTPUT_FIELDS = (
    ("TYPE", 0, 3),
    ("ON", 500, 2500),
    ("OFF", 500, 2500),
    ("FS", 500, 2500),
    ("FS_STATE", 0, 1),
    ("TIME", 1, 60000),
    ("RETRIG", 0, 1),
)

DEFAULT_OUTPUT = {
    "TYPE": 1,
    "ON": 2000,
    "OFF": 1000,
    "FS": 1500,
    "FS_STATE": 0,
    "TIME": 1000,
    "RETRIG": 0,
}


def make_dark_palette():
    p = QPalette()
    p.setColor(QPalette.Window, QColor(37, 37, 38))
    p.setColor(QPalette.WindowText, QColor(230, 230, 230))
    p.setColor(QPalette.Base, QColor(30, 30, 30))
    p.setColor(QPalette.AlternateBase, QColor(45, 45, 45))
    p.setColor(QPalette.ToolTipBase, QColor(255, 255, 220))
    p.setColor(QPalette.ToolTipText, QColor(20, 20, 20))
    p.setColor(QPalette.Text, QColor(235, 235, 235))
    p.setColor(QPalette.Button, QColor(50, 50, 52))
    p.setColor(QPalette.ButtonText, QColor(235, 235, 235))
    p.setColor(QPalette.BrightText, QColor(255, 80, 80))
    p.setColor(QPalette.Highlight, QColor(58, 123, 213))
    p.setColor(QPalette.HighlightedText, QColor(255, 255, 255))
    p.setColor(QPalette.PlaceholderText, QColor(150, 150, 150))
    return p


class WorkerSignals(QObject):
    finished = Signal(object)
    failed = Signal(str)
    log = Signal(str)


class DroneCANClient:
    def __init__(self, iface, bitrate, target_node_id, log_callback=None):
        if dronecan is None:
            raise RuntimeError(
                "Модуль Python 'dronecan' не установлен.\n"
                "Установите его в используемое virtualenv."
            )
        self.iface = iface
        self.bitrate = int(bitrate)
        self.target_node_id = int(target_node_id)
        self.log_callback = log_callback
        self.node = None

    def log(self, text):
        if self.log_callback:
            self.log_callback(text)

    def open(self):
        if self.node is None:
            self.log(
                f"Открытие {self.iface}, bitrate={self.bitrate}, "
                f"local node={LOCAL_NODE_ID}, target={self.target_node_id}"
            )
            self.node = dronecan.make_node(
                self.iface,
                node_id=LOCAL_NODE_ID,
                bitrate=self.bitrate,
            )

    def close(self):
        if self.node is not None:
            try:
                self.node.close()
            finally:
                self.node = None

    def request_sync(self, request, timeout=2.0, attempts=3, retry_delay=0.08):
        self.open()

        attempts = max(1, int(attempts))
        last_event = None

        for attempt in range(1, attempts + 1):
            result = {"done": False, "event": None}

            def callback(event, result=result):
                result["done"] = True
                result["event"] = event

            self.node.request(
                request,
                self.target_node_id,
                callback,
                timeout=1.0,
            )

            deadline = time.monotonic() + timeout
            while not result["done"] and time.monotonic() < deadline:
                self.node.spin(0.05)

            last_event = result["event"]
            if last_event is not None:
                return last_event

            if attempt < attempts:
                self.log(
                    f"Нет ответа от node {self.target_node_id}; "
                    f"повтор {attempt + 1}/{attempts}"
                )
                pause_deadline = time.monotonic() + retry_delay
                while time.monotonic() < pause_deadline:
                    self.node.spin(0.02)

        return last_event

    @staticmethod
    def _integer_value(response):
        try:
            if "empty" in str(response.value).lower():
                return None
            return int(response.value.integer_value)
        except (AttributeError, TypeError, ValueError):
            return None

    def read_param(self, name):
        req = dronecan.uavcan.protocol.param.GetSet.Request()
        req.name = name
        event = self.request_sync(req)
        if event is None:
            raise TimeoutError(f"{name}: TIMEOUT")
        value = self._integer_value(event.response)
        if value is None:
            raise KeyError(f"{name}: NOT FOUND")
        return value

    def write_param(self, name, value):
        req = dronecan.uavcan.protocol.param.GetSet.Request()
        req.name = name
        req.value.integer_value = int(value)
        event = self.request_sync(req)
        if event is None:
            raise TimeoutError(f"{name}: WRITE TIMEOUT")
        returned = self._integer_value(event.response)
        if returned is None:
            raise KeyError(f"{name}: WRITE FAILED / NOT FOUND")
        if returned != int(value):
            raise RuntimeError(
                f"{name}: requested={value}, returned={returned}"
            )
        return returned

    def execute_opcode(self, opcode):
        req = dronecan.uavcan.protocol.param.ExecuteOpcode.Request()
        req.opcode = int(opcode)
        req.argument = 0
        event = self.request_sync(req)
        if event is None:
            raise TimeoutError("ExecuteOpcode: TIMEOUT")
        return bool(event.response.ok), int(event.response.argument)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"{APP_TITLE} — {APP_REVISION}")
        self.resize(1000, 820)

        self.settings = QSettings("DroneCAN", "OutputControllerConfigurator")
        self.dark = False
        self.busy = False
        self.loading_ui = False
        self.baseline = None              # последнее известное состояние RAM узла
        # Параметры, которые ЭТА программа успешно записала в RAM после последнего SAVE.
        # Это не попытка угадать содержимое Flash.
        self.pending_flash_params = set()
        self.controller_markers = {}      # NODE_ID/CAN_BITRATE/PCA_COUNT/FS_TIMEOUT -> QLabel
        self._worker_signals = []

        self._build_top_panel()
        self._build_menu()
        self._build_central()
        self._build_status()
        self._connect_change_tracking()

        saved_dark = self.settings.value("ui/dark_theme", False, type=bool)
        self.theme_switch.setChecked(saved_dark)
        self.menu_dark_theme.setChecked(saved_dark)
        self.apply_theme(saved_dark)

        saved_geometry = self.settings.value("window/geometry")
        if saved_geometry is not None:
            self.restoreGeometry(saved_geometry)
        else:
            self._resize_to_outputs_width()

        self._set_default_configuration()
        self.status.showMessage("Не подключено")

    # ---------- UI ----------

    def _resize_to_outputs_width(self):
        self.table.resizeColumnsToContents()
        self.table.setColumnWidth(0, 24)
        self.table.setColumnWidth(2, 140)

        table_width = self.table.verticalHeader().width()
        for col in range(self.table.columnCount()):
            table_width += self.table.columnWidth(col)
        table_width += self.table.verticalScrollBar().sizeHint().width()
        table_width += self.table.frameWidth() * 2 + 18

        desired_width = table_width + 42
        self.top_panel.adjustSize()
        desired_width = max(desired_width, self.top_panel.sizeHint().width() + 34)

        screen = QApplication.screenAt(QCursor.pos())
        if screen is None:
            screen = QApplication.primaryScreen()
        if screen is not None:
            available = screen.availableGeometry()
            desired_width = min(desired_width, available.width() - 40)

        self.resize(desired_width, self.height())

        if screen is not None:
            available = screen.availableGeometry()
            frame = self.frameGeometry()
            frame.moveCenter(available.center())
            self.move(frame.topLeft())

    def _build_top_panel(self):
        self.top_panel = QWidget()
        self.top_panel.setObjectName("topPanel")
        outer = QVBoxLayout(self.top_panel)
        outer.setContentsMargins(8, 6, 8, 6)
        outer.setSpacing(6)

        row1 = QHBoxLayout()
        row1.setSpacing(8)

        row1.addWidget(QLabel("CAN:"))
        self.iface = QLineEdit(DEFAULT_CAN_IFACE)
        self.iface.setFixedWidth(90)
        self.iface.setToolTip("Локальный Linux SocketCAN интерфейс, обычно can0")
        row1.addWidget(self.iface)

        row1.addSpacing(8)
        row1.addWidget(QLabel("Bitrate:"))
        self.bitrate = QComboBox()
        self.bitrate.addItems([str(v) for v in CAN_BITRATES])
        self.bitrate.setCurrentText(str(DEFAULT_CAN_BITRATE))
        self.bitrate.setFixedWidth(120)
        self.bitrate.setToolTip(
            "Скорость локальной CAN-шины. Должна совпадать со скоростью контроллера."
        )
        row1.addWidget(self.bitrate)

        row1.addSpacing(8)
        row1.addWidget(QLabel("Node ID:"))
        self.node_id = QSpinBox()
        self.node_id.setRange(1, 125)
        self.node_id.setValue(DEFAULT_TARGET_NODE_ID)
        self.node_id.setFixedWidth(80)
        self.node_id.setToolTip("Текущий Node ID контроллера, к которому идут запросы")
        row1.addWidget(self.node_id)

        row1.addStretch()

        self.theme_switch = QCheckBox("Тёмная тема")
        self.theme_switch.toggled.connect(self.apply_theme)
        row1.addWidget(self.theme_switch)

        row2 = QHBoxLayout()
        row2.setSpacing(8)

        self.read_btn = QPushButton("Прочитать узел")
        self.write_btn = QPushButton("Записать в узел")
        self.flash_btn = QPushButton("SAVE Flash")
        self.open_btn = QPushButton("Открыть файл")
        self.save_btn = QPushButton("Сохранить файл")
        self.erase_btn = QPushButton("ERASE")

        self.read_btn.setToolTip(
            "Прочитать текущую конфигурацию контроллера по DroneCAN и принять её как состояние RAM."
        )
        self.write_btn.setToolTip(
            "Записать в RAM контроллера только параметры, изменённые относительно последнего чтения/записи. "
            "Для постоянного хранения затем выполните SAVE Flash."
        )
        self.flash_btn.setToolTip(
            "Сохранить текущую RAM-конфигурацию контроллера во Flash. "
            "После этого настройки сохранятся после отключения питания."
        )
        self.open_btn.setToolTip(
            "Загрузить конфигурацию из JSON-файла в GUI. Файл сам по себе ничего не записывает в контроллер."
        )
        self.save_btn.setToolTip(
            "Сохранить текущую конфигурацию GUI в JSON-файл."
        )
        self.erase_btn.setToolTip(
            "Удалить сохранённую конфигурацию из Flash контроллера через DroneCAN ExecuteOpcode ERASE."
        )
        self.theme_switch.setToolTip("Переключить светлую/тёмную тему интерфейса.")

        for button in (
            self.read_btn, self.write_btn, self.flash_btn,
            self.open_btn, self.save_btn, self.erase_btn
        ):
            row2.addWidget(button)

        row2.addStretch()
        outer.addLayout(row1)
        outer.addLayout(row2)

        self.read_btn.clicked.connect(self.read_node)
        self.write_btn.clicked.connect(self.write_node)
        self.flash_btn.clicked.connect(self.save_flash)
        self.open_btn.clicked.connect(self.load_file)
        self.save_btn.clicked.connect(self.save_file)
        self.erase_btn.clicked.connect(self.erase_flash)

    def _build_menu(self):
        menu_bar = self.menuBar()

        file_menu = menu_bar.addMenu("Файл")

        self.menu_connect = QAction("Открыть соединение", self)
        self.menu_connect.setShortcut("Ctrl+Shift+C")
        self.menu_connect.setToolTip("Проверить доступность DroneCAN-контроллера по выбранному CAN-интерфейсу.")
        self.menu_connect.triggered.connect(self.open_connection)
        file_menu.addAction(self.menu_connect)

        file_menu.addSeparator()

        self.menu_open_file = QAction("Прочитать файл...", self)
        self.menu_open_file.setShortcut("Ctrl+O")
        self.menu_open_file.triggered.connect(self.load_file)
        file_menu.addAction(self.menu_open_file)

        self.menu_save_file = QAction("Сохранить файл...", self)
        self.menu_save_file.setShortcut("Ctrl+S")
        self.menu_save_file.triggered.connect(self.save_file)
        file_menu.addAction(self.menu_save_file)

        file_menu.addSeparator()

        self.menu_exit = QAction("Выход", self)
        self.menu_exit.setShortcut("Ctrl+Q")
        self.menu_exit.triggered.connect(self.close)
        file_menu.addAction(self.menu_exit)

        device_menu = menu_bar.addMenu("Устройство")

        self.menu_read_node = QAction("Прочитать узел", self)
        self.menu_read_node.setShortcut("F5")
        self.menu_read_node.setToolTip("Прочитать текущую конфигурацию узла в GUI.")
        self.menu_read_node.triggered.connect(self.read_node)
        device_menu.addAction(self.menu_read_node)

        self.menu_write_node = QAction("Записать в узел", self)
        self.menu_write_node.setShortcut("F6")
        self.menu_write_node.setToolTip("Записать в RAM только изменённые параметры.")
        self.menu_write_node.triggered.connect(self.write_node)
        device_menu.addAction(self.menu_write_node)

        device_menu.addSeparator()

        self.menu_save_flash = QAction("SAVE Flash", self)
        self.menu_save_flash.setShortcut("Ctrl+Shift+S")
        self.menu_save_flash.setToolTip("Сохранить текущую RAM-конфигурацию во Flash.")
        self.menu_save_flash.triggered.connect(self.save_flash)
        device_menu.addAction(self.menu_save_flash)

        self.menu_erase = QAction("ERASE Flash...", self)
        self.menu_erase.setToolTip("Удалить сохранённую конфигурацию из Flash контроллера.")
        self.menu_erase.triggered.connect(self.erase_flash)
        device_menu.addAction(self.menu_erase)

        view_menu = menu_bar.addMenu("Вид")

        self.menu_dark_theme = QAction("Тёмная тема", self)
        self.menu_dark_theme.setCheckable(True)
        self.menu_dark_theme.toggled.connect(self.theme_switch.setChecked)
        self.theme_switch.toggled.connect(self.menu_dark_theme.setChecked)
        view_menu.addAction(self.menu_dark_theme)

        help_menu = menu_bar.addMenu("Помощь")

        self.menu_help = QAction("Помощь", self)
        self.menu_help.setShortcut("F1")
        self.menu_help.triggered.connect(self.show_help)
        help_menu.addAction(self.menu_help)

        help_menu.addSeparator()

        self.menu_about = QAction("О программе", self)
        self.menu_about.triggered.connect(self.show_about)
        help_menu.addAction(self.menu_about)

    def _build_central(self):
        root = QWidget()
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(8, 8, 8, 8)
        root_layout.setSpacing(6)
        root_layout.addWidget(self.top_panel)

        self.tabs = QTabWidget()
        root_layout.addWidget(self.tabs)

        self.system_tab = QWidget()
        self.outputs_tab = QWidget()
        self.log_tab = QWidget()

        self.tabs.addTab(self.system_tab, "Контроллер")
        self.tabs.addTab(self.outputs_tab, "Выходы")
        self.tabs.addTab(self.log_tab, "Журнал")

        self._build_system_tab()
        self._build_outputs_tab()
        self._build_log_tab()

        self.setCentralWidget(root)

    def _build_system_tab(self):
        lay = QVBoxLayout(self.system_tab)
        lay.setContentsMargins(16, 16, 16, 16)
        lay.setSpacing(12)

        title = QLabel("Параметры контроллера")
        title.setObjectName("title")
        lay.addWidget(title)

        form = QWidget()
        form.setObjectName("controllerForm")
        grid = QVBoxLayout(form)
        grid.setContentsMargins(14, 14, 14, 14)
        grid.setSpacing(10)

        def add_row(param_name, label_text, editor, description):
            row = QHBoxLayout()
            row.setSpacing(12)

            marker = QLabel("")
            marker.setFixedWidth(18)
            marker.setAlignment(Qt.AlignCenter)
            marker.setToolTip(
                "● — значение изменено в GUI и ещё не записано в RAM.\n"
                "◆ — значение уже записано в RAM, но ещё не сохранено во Flash."
            )
            row.addWidget(marker)
            self.controller_markers[param_name] = marker

            label = QLabel(label_text)
            label.setFixedWidth(140)
            label.setToolTip(description)
            row.addWidget(label)

            editor.setFixedWidth(150)
            editor.setToolTip(description)
            row.addWidget(editor)

            desc = QLabel(description)
            desc.setWordWrap(True)
            desc.setObjectName("paramDesc")
            desc.setToolTip(description)
            row.addWidget(desc, 1)
            grid.addLayout(row)

        self.ctrl_node_id = QSpinBox()
        self.ctrl_node_id.setRange(1, 125)
        add_row(
            "NODE_ID", "Node ID", self.ctrl_node_id,
            "Сохраняемый адрес контроллера в сети DroneCAN. "
            "Записывается последним; применение может потребовать перезапуска."
        )

        self.ctrl_bitrate = QComboBox()
        self.ctrl_bitrate.addItems([str(v) for v in CAN_BITRATES])
        add_row(
            "CAN_BITRATE", "CAN bitrate", self.ctrl_bitrate,
            "Сохраняемая скорость CAN контроллера. "
            "Должна совпадать с остальными устройствами шины."
        )

        self.ctrl_pca_count = QComboBox()
        self.ctrl_pca_count.addItems(["1", "2"])
        add_row(
            "PCA_COUNT", "PCA9685", self.ctrl_pca_count,
            "Количество модулей выходов: 1 = OUT01…OUT16, 2 = OUT01…OUT32."
        )

        self.ctrl_fs_timeout = QSpinBox()
        self.ctrl_fs_timeout.setRange(10, 60000)
        self.ctrl_fs_timeout.setSuffix(" ms")
        add_row(
            "FS_TIMEOUT", "Failsafe timeout", self.ctrl_fs_timeout,
            "Время без валидных команд до перехода выходов "
            "в настроенное безопасное состояние."
        )

        lay.addWidget(form)

        note = QLabel(
            "«Записать в узел» изменяет RAM контроллера. "
            "Для постоянного хранения после проверки нажмите SAVE Flash."
        )
        note.setObjectName("note")
        lay.addWidget(note)
        lay.addStretch()

    def _build_outputs_tab(self):
        lay = QVBoxLayout(self.outputs_tab)

        self.table = QTableWidget(32, 9)
        self.table.setHorizontalHeaderLabels(
            ["", "OUT", "TYPE", "ON µs", "OFF µs", "FS µs",
             "FS STATE", "TIME ms", "RETRIG"]
        )
        self.table.verticalHeader().setVisible(False)
        self.table.setAlternatingRowColors(True)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setSelectionMode(QTableWidget.SingleSelection)
        self.table.setSortingEnabled(False)

        h = self.table.horizontalHeader()
        h.setSectionResizeMode(0, QHeaderView.Fixed)
        self.table.setColumnWidth(0, 24)
        h.setSectionResizeMode(1, QHeaderView.ResizeToContents)
        h.setSectionResizeMode(2, QHeaderView.Fixed)
        self.table.setColumnWidth(2, 140)
        for c in range(3, 9):
            h.setSectionResizeMode(c, QHeaderView.ResizeToContents)

        header_tooltips = {
            0: "Маркер состояния строки: ● — изменено в GUI и ещё не записано в RAM; ◆ — RAM отличается от Flash.",
            1: "Номер логического выхода контроллера. OUT01…OUT16 относятся к PCA9685 #1, OUT17…OUT32 — к PCA9685 #2.",
            2: "Режим выхода: DISABLED — отключён; PWM — прямое PWM; ON_OFF — логическое ВКЛ/ВЫКЛ; PULSE — импульс по фронту OFF→ON.",
            3: "ON µs — физическое PWM-значение для логического состояния ON. Используется в ON_OFF и PULSE.",
            4: "OFF µs — физическое PWM-значение для логического состояния OFF. Используется в ON_OFF и PULSE.",
            5: "FS µs — безопасное PWM-значение при failsafe. Используется только для TYPE=PWM.",
            6: "FS STATE — безопасное логическое состояние при failsafe: 0=OFF, 1=ON. Используется для ON_OFF и PULSE.",
            7: "TIME ms — длительность импульса для TYPE=PULSE в миллисекундах.",
            8: "RETRIG для PULSE: IGNORE — повторный ON во время импульса игнорируется; RESTART — повторный ON перезапускает таймер импульса.",
        }
        for col, tip in header_tooltips.items():
            item = self.table.horizontalHeaderItem(col)
            if item is not None:
                item.setToolTip(tip)

        for row in range(32):
            idx = row + 1

            marker = QTableWidgetItem("")
            marker.setTextAlignment(Qt.AlignCenter)
            marker.setFlags(marker.flags() & ~Qt.ItemIsEditable)
            marker.setToolTip(
                "● — изменено в GUI и ещё не записано в RAM; ◆ — RAM отличается от Flash."
            )
            self.table.setItem(row, 0, marker)

            out_item = QTableWidgetItem(f"OUT{idx:02d}")
            out_item.setFlags(out_item.flags() & ~Qt.ItemIsEditable)
            out_item.setToolTip(
                f"Логический выход OUT{idx:02d}. Активность строки зависит от PCA_COUNT."
            )
            self.table.setItem(row, 1, out_item)

            type_box = QComboBox()
            type_box.addItems(TYPE_ITEMS)
            type_box.setToolTip(
                "Режим выхода: DISABLED — отключён; PWM — прямое PWM; "
                "ON_OFF — логическое ВКЛ/ВЫКЛ; PULSE — импульс по фронту OFF→ON."
            )
            type_box.currentTextChanged.connect(
                lambda _value, r=row: self._output_edited(r, refresh=True)
            )
            self.table.setCellWidget(row, 2, type_box)

            edit_tooltips = {
                3: "ON µs: PWM-значение выхода для логического ON. Используется в ON_OFF и PULSE.",
                4: "OFF µs: PWM-значение выхода для логического OFF. Используется в ON_OFF и PULSE.",
                5: "FS µs: PWM-значение при failsafe. Используется только в режиме PWM.",
                6: "FS STATE: 0=OFF, 1=ON при failsafe. Используется в ON_OFF и PULSE.",
                7: "TIME ms: длительность импульса в режиме PULSE.",
            }
            for col in range(3, 8):
                edit = QLineEdit()
                edit.setAlignment(Qt.AlignCenter)
                edit.setToolTip(edit_tooltips[col])
                edit.textEdited.connect(
                    lambda _value, r=row: self._output_edited(r)
                )
                self.table.setCellWidget(row, col, edit)

            retrig = QComboBox()
            retrig.addItems(RETRIG_ITEMS)
            retrig.setToolTip(
                "Поведение повторного ON в режиме PULSE: IGNORE — не продлевать активный импульс; "
                "RESTART — начать отсчёт TIME заново."
            )
            retrig.currentTextChanged.connect(
                lambda _value, r=row: self._output_edited(r)
            )
            self.table.setCellWidget(row, 8, retrig)

        self.table.viewport().setToolTip(
            "Параметры выходов OUT01…OUT32. Наведите указатель на заголовок или конкретное поле для подробной подсказки."
        )
        lay.addWidget(self.table)

        legend = QHBoxLayout()
        self.changed_label = QLabel("Изменено: 0")
        legend.addWidget(self.changed_label)
        legend.addStretch()
        legend_label = QLabel("● — изменено в GUI, не записано в RAM    ◆ — RAM отличается от Flash")
        legend_label.setToolTip(
            "● означает, что значение изменено только в GUI. После «Записать в узел» оно попадёт в RAM. "
            "◆ означает, что RAM уже изменена, но SAVE Flash ещё не выполнен."
        )
        legend.addWidget(legend_label)
        lay.addLayout(legend)

    def _build_log_tab(self):
        lay = QVBoxLayout(self.log_tab)
        self.log = QTextEdit()
        self.log.setReadOnly(True)
        self.log.setPlaceholderText("Журнал обмена с DroneCAN контроллером")
        lay.addWidget(self.log)

    def _build_status(self):
        self.status = QStatusBar()
        self.setStatusBar(self.status)

    def _connect_change_tracking(self):
        self.ctrl_node_id.valueChanged.connect(self._configuration_edited)
        self.ctrl_bitrate.currentTextChanged.connect(self._configuration_edited)
        self.ctrl_pca_count.currentTextChanged.connect(self._pca_count_edited)
        self.ctrl_fs_timeout.valueChanged.connect(self._configuration_edited)

    # ---------- Configuration/UI mapping ----------

    def _set_default_configuration(self):
        sysv = {
            "NODE_ID": DEFAULT_TARGET_NODE_ID,
            "CAN_BITRATE": DEFAULT_CAN_BITRATE,
            "PCA_COUNT": 1,
            "FS_TIMEOUT": 700,
        }
        outv = {i: dict(DEFAULT_OUTPUT) for i in range(1, 17)}
        self._apply_config(sysv, outv, set_baseline=False)

    def _set_active_outputs(self, count):
        self.loading_ui = True
        try:
            for row in range(32):
                active = row < count
                for col in range(2, 9):
                    widget = self.table.cellWidget(row, col)
                    if widget is not None:
                        widget.setEnabled(active)

                for col in (0, 1):
                    item = self.table.item(row, col)
                    if item is not None:
                        flags = item.flags()
                        if active:
                            item.setFlags(flags | Qt.ItemIsEnabled)
                        else:
                            item.setFlags(flags & ~Qt.ItemIsEnabled)

                if active:
                    self.refresh_row(row, mark=False)
                else:
                    marker = self.table.item(row, 0)
                    if marker:
                        marker.setText("")
        finally:
            self.loading_ui = False

    def _ensure_output_defaults(self, first_index, last_index):
        self.loading_ui = True
        try:
            for idx in range(first_index, last_index + 1):
                row = idx - 1
                type_box = self.table.cellWidget(row, 2)
                if type_box.currentText() not in TYPE_TO_INT:
                    type_box.setCurrentText("PWM")

                values = {
                    3: DEFAULT_OUTPUT["ON"],
                    4: DEFAULT_OUTPUT["OFF"],
                    5: DEFAULT_OUTPUT["FS"],
                    6: DEFAULT_OUTPUT["FS_STATE"],
                    7: DEFAULT_OUTPUT["TIME"],
                }
                for col, default in values.items():
                    edit = self.table.cellWidget(row, col)
                    if not edit.text().strip():
                        edit.setText(str(default))

                retrig = self.table.cellWidget(row, 8)
                if retrig.currentText() not in RETRIG_TO_INT:
                    retrig.setCurrentText("IGNORE")
        finally:
            self.loading_ui = False

    def refresh_row(self, row, mark=True):
        count = int(self.ctrl_pca_count.currentText()) * 16
        if row >= count:
            return

        typ = self.table.cellWidget(row, 2).currentText()
        widgets = [self.table.cellWidget(row, c) for c in range(3, 9)]
        states = {
            "DISABLED": [False, False, False, False, False, False],
            "PWM":      [False, False, True,  False, False, False],
            "ON_OFF":   [True,  True,  False, True,  False, False],
            "PULSE":    [True,  True,  False, True,  True,  True],
        }[typ]

        for widget, enabled in zip(widgets, states):
            widget.setEnabled(enabled)

        if mark and not self.loading_ui:
            self._update_dirty_state()

    def _output_edited(self, row, refresh=False):
        if self.loading_ui:
            return
        if refresh:
            self.refresh_row(row, mark=False)
        self._update_dirty_state()

    def _configuration_edited(self, *_args):
        if self.loading_ui:
            return
        self._update_dirty_state()

    def _pca_count_edited(self, *_args):
        if self.loading_ui:
            return
        count = int(self.ctrl_pca_count.currentText()) * 16
        self._ensure_output_defaults(1, count)
        self._set_active_outputs(count)
        self._update_dirty_state()

    def _collect_values(self):
        sysv = {
            "NODE_ID": int(self.ctrl_node_id.value()),
            "CAN_BITRATE": int(self.ctrl_bitrate.currentText()),
            "PCA_COUNT": int(self.ctrl_pca_count.currentText()),
            "FS_TIMEOUT": int(self.ctrl_fs_timeout.value()),
        }

        for name, lo, hi in SYSTEM_PARAMS:
            value = sysv[name]
            if not lo <= value <= hi:
                raise ValueError(f"{name}: {value}, допустимо {lo}..{hi}")

        if sysv["CAN_BITRATE"] not in CAN_BITRATES:
            raise ValueError("CAN_BITRATE: неподдерживаемое значение")

        count = sysv["PCA_COUNT"] * 16
        outv = {}
        for idx in range(1, count + 1):
            row = idx - 1
            typ = TYPE_TO_INT[self.table.cellWidget(row, 2).currentText()]
            retrig = RETRIG_TO_INT[self.table.cellWidget(row, 8).currentText()]

            raw = {
                "TYPE": typ,
                "ON": self.table.cellWidget(row, 3).text().strip(),
                "OFF": self.table.cellWidget(row, 4).text().strip(),
                "FS": self.table.cellWidget(row, 5).text().strip(),
                "FS_STATE": self.table.cellWidget(row, 6).text().strip(),
                "TIME": self.table.cellWidget(row, 7).text().strip(),
                "RETRIG": retrig,
            }

            vals = {}
            for field, lo, hi in OUTPUT_FIELDS:
                if field in ("TYPE", "RETRIG"):
                    value = int(raw[field])
                else:
                    if not raw[field]:
                        raise ValueError(f"OUT{idx:02d}_{field}: пустое значение")
                    try:
                        value = int(raw[field])
                    except ValueError:
                        raise ValueError(
                            f"OUT{idx:02d}_{field}: требуется целое число"
                        )
                if not lo <= value <= hi:
                    raise ValueError(
                        f"OUT{idx:02d}_{field}: {value}, допустимо {lo}..{hi}"
                    )
                vals[field] = value
            outv[idx] = vals

        return sysv, outv

    @staticmethod
    def _flat_config(sysv, outv):
        flat = {}
        for name, value in sysv.items():
            flat[name] = int(value)
        for idx, values in outv.items():
            for field, value in values.items():
                flat[f"OUT{idx:02d}_{field}"] = int(value)
        return flat

    def _current_flat_safe(self):
        try:
            sysv, outv = self._collect_values()
            return self._flat_config(sysv, outv)
        except Exception:
            return None

    def _update_dirty_state(self):
        current = self._current_flat_safe()

        if self.baseline is None or current is None:
            for marker in self.controller_markers.values():
                marker.setText("")
            for row in range(32):
                marker = self.table.item(row, 0)
                if marker:
                    marker.setText("")
            self.changed_label.setText("GUI→RAM: 0    RAM→Flash: неизвестно")
            self.flash_btn.setEnabled(False if not self.busy else False)
            self.menu_save_flash.setEnabled(False if not self.busy else False)
            return

        active_count = int(self.ctrl_pca_count.currentText()) * 16
        gui_changed_rows = 0
        flash_changed_rows = 0

        for idx in range(1, 33):
            marker = self.table.item(idx - 1, 0)
            if idx > active_count:
                marker.setText("")
                continue

            gui_changed = False
            flash_changed = False
            for field, _lo, _hi in OUTPUT_FIELDS:
                name = f"OUT{idx:02d}_{field}"
                if current.get(name) != self.baseline.get(name):
                    gui_changed = True
                if name in self.pending_flash_params:
                    flash_changed = True

            # Изменения GUI имеют приоритет визуально над несохранённой RAM.
            if gui_changed:
                marker.setText("●")
                gui_changed_rows += 1
            elif flash_changed:
                marker.setText("◆")
                flash_changed_rows += 1
            else:
                marker.setText("")

        gui_changed_system = False
        flash_changed_system = False

        for name, _lo, _hi in SYSTEM_PARAMS:
            marker = self.controller_markers.get(name)
            gui_changed = current.get(name) != self.baseline.get(name)
            flash_changed = name in self.pending_flash_params

            # Как и для OUTxx: изменение GUI имеет визуальный приоритет.
            if marker is not None:
                if gui_changed:
                    marker.setText("●")
                elif flash_changed:
                    marker.setText("◆")
                else:
                    marker.setText("")

            gui_changed_system = gui_changed_system or gui_changed
            flash_changed_system = flash_changed_system or flash_changed

        gui_suffix = " + контроллер" if gui_changed_system else ""
        flash_suffix = " + контроллер" if flash_changed_system else ""
        self.changed_label.setText(
            f"GUI→RAM: {gui_changed_rows}{gui_suffix}    "
            f"RAM→Flash: {flash_changed_rows}{flash_suffix}"
        )

        # SAVE доступен только когда эта GUI-сессия сама записала что-то в RAM
        # и это ещё не было сохранено командой SAVE Flash.
        has_pending_flash = bool(self.pending_flash_params)
        if not self.busy:
            self.flash_btn.setEnabled(has_pending_flash)
            self.menu_save_flash.setEnabled(has_pending_flash)

    def _apply_config(self, sysv, outv, set_baseline=False):
        self.loading_ui = True
        try:
            self.ctrl_node_id.setValue(int(sysv["NODE_ID"]))
            self.ctrl_bitrate.setCurrentText(str(int(sysv["CAN_BITRATE"])))
            self.ctrl_pca_count.setCurrentText(str(int(sysv["PCA_COUNT"])))
            self.ctrl_fs_timeout.setValue(int(sysv["FS_TIMEOUT"]))

            count = int(sysv["PCA_COUNT"]) * 16
            self._ensure_output_defaults(1, count)

            for idx in range(1, count + 1):
                vals = outv[idx]
                row = idx - 1

                self.table.cellWidget(row, 2).setCurrentText(
                    INT_TO_TYPE[int(vals["TYPE"])]
                )
                self.table.cellWidget(row, 3).setText(str(int(vals["ON"])))
                self.table.cellWidget(row, 4).setText(str(int(vals["OFF"])))
                self.table.cellWidget(row, 5).setText(str(int(vals["FS"])))
                self.table.cellWidget(row, 6).setText(str(int(vals["FS_STATE"])))
                self.table.cellWidget(row, 7).setText(str(int(vals["TIME"])))
                self.table.cellWidget(row, 8).setCurrentText(
                    INT_TO_RETRIG[int(vals["RETRIG"])]
                )

            self._set_active_outputs(count)
            for row in range(count):
                self.refresh_row(row, mark=False)
        finally:
            self.loading_ui = False

        if set_baseline:
            new_baseline = self._flat_config(sysv, outv)

            # "Прочитать узел" сообщает только фактическую RAM-конфигурацию.
            # Оно ничего не говорит о содержимом Flash.
            #
            # Если ранее ЭТА GUI записала параметры в RAM без SAVE, сохраняем ◆
            # только для тех параметров, у которых повторное чтение подтвердило
            # именно записанное нами значение. После reboot/ERASE/внешнего изменения
            # такой маркер автоматически исчезнет.
            if self.baseline is not None and self.pending_flash_params:
                self.pending_flash_params = {
                    name
                    for name in self.pending_flash_params
                    if name in new_baseline
                    and self.baseline.get(name) == new_baseline.get(name)
                }
            else:
                self.pending_flash_params.clear()

            self.baseline = new_baseline

        self._update_dirty_state()

    # ---------- Worker ----------

    def _set_busy(self, busy, text=None):
        self.busy = busy

        buttons = (
            self.read_btn, self.write_btn, self.flash_btn,
            self.open_btn, self.save_btn, self.erase_btn,
        )
        actions = (
            self.menu_connect, self.menu_read_node, self.menu_write_node,
            self.menu_save_flash, self.menu_erase,
            self.menu_open_file, self.menu_save_file,
        )

        for widget in buttons:
            widget.setEnabled(not busy)
        for action in actions:
            action.setEnabled(not busy)

        if not busy:
            self._update_dirty_state()

        if text:
            self.status.showMessage(text)

    def _run_worker(self, title, function, on_success=None):
        if self.busy:
            return

        signals = WorkerSignals()
        self._worker_signals.append(signals)

        signals.log.connect(self.append_log)

        def finished(payload):
            self._set_busy(False, "Готово")
            try:
                if on_success is not None:
                    on_success(payload)
            finally:
                if signals in self._worker_signals:
                    self._worker_signals.remove(signals)

        def failed(message):
            self._set_busy(False, "Ошибка")
            self.append_log(f"ERROR: {message}")
            QMessageBox.critical(self, APP_TITLE, message)
            if signals in self._worker_signals:
                self._worker_signals.remove(signals)

        signals.finished.connect(finished)
        signals.failed.connect(failed)

        self._set_busy(True, title)

        def runner():
            try:
                payload = function(signals.log.emit)
                signals.finished.emit(payload)
            except Exception as exc:
                signals.failed.emit(str(exc))

        threading.Thread(target=runner, daemon=True).start()

    def _connection_values(self):
        iface = self.iface.text().strip()
        if not iface:
            raise ValueError("Не задан SocketCAN интерфейс")
        bitrate = int(self.bitrate.currentText())
        target = int(self.node_id.value())
        return iface, bitrate, target

    # ---------- CAN operations ----------

    def open_connection(self):
        try:
            iface, bitrate, target = self._connection_values()
        except Exception as exc:
            QMessageBox.critical(self, APP_TITLE, str(exc))
            return

        def task(log):
            client = DroneCANClient(iface, bitrate, target, log)
            try:
                node_id = client.read_param("NODE_ID")
                pca_count = client.read_param("PCA_COUNT")
                return {"node_id": node_id, "pca_count": pca_count}
            finally:
                client.close()

        def done(data):
            self.append_log(
                f"Связь с node {target} установлена: "
                f"NODE_ID={data['node_id']}, PCA_COUNT={data['pca_count']}"
            )
            self.status.showMessage(f"CAN OK — node {target}")

        self._run_worker("Проверка соединения...", task, done)

    def read_node(self):
        try:
            iface, bitrate, target = self._connection_values()
        except Exception as exc:
            QMessageBox.critical(self, APP_TITLE, str(exc))
            return

        def task(log):
            client = DroneCANClient(iface, bitrate, target, log)
            try:
                log(f"Чтение системных параметров node {target}")
                sysv = {
                    name: client.read_param(name)
                    for name, _lo, _hi in SYSTEM_PARAMS
                }

                count = max(1, min(2, int(sysv["PCA_COUNT"]))) * 16
                outv = {}

                for idx in range(1, count + 1):
                    vals = {}
                    for field, _lo, _hi in OUTPUT_FIELDS:
                        name = f"OUT{idx:02d}_{field}"
                        vals[field] = client.read_param(name)
                    outv[idx] = vals
                    log(f"OUT{idx:02d}: прочитан")

                return sysv, outv
            finally:
                client.close()

        def done(payload):
            sysv, outv = payload
            self._apply_config(sysv, outv, set_baseline=True)

            # Поле сверху — адрес, по которому мы реально только что общались.
            # Не меняем его автоматически при загрузке файлов.
            self.node_id.setValue(target)

            self.append_log(
                f"Узел прочитан: NODE_ID={sysv['NODE_ID']}, "
                f"PCA_COUNT={sysv['PCA_COUNT']}, outputs={len(outv)}"
            )
            self.status.showMessage(f"CAN OK — node {target}, конфигурация прочитана")

        self._run_worker("Чтение узла...", task, done)

    def write_node(self):
        try:
            iface, bitrate, target = self._connection_values()
            sysv, outv = self._collect_values()
        except Exception as exc:
            QMessageBox.critical(self, APP_TITLE, str(exc))
            return

        if self.baseline is None:
            QMessageBox.warning(
                self,
                APP_TITLE,
                "Нет исходного состояния узла.\n\n"
                "Сначала нажмите «Прочитать узел», затем внесите изменения "
                "и повторите запись.",
            )
            return

        current_flat = self._flat_config(sysv, outv)
        changed = {
            name: value
            for name, value in current_flat.items()
            if self.baseline.get(name) != value
        }

        if not changed:
            QMessageBox.information(
                self, APP_TITLE, "Нет изменений для записи в контроллер."
            )
            self.status.showMessage("Нет изменений")
            return

        changed_names = list(changed.keys())
        preview = "\n".join(
            f"  {name}: {self.baseline.get(name, '—')} -> {changed[name]}"
            for name in changed_names[:12]
        )
        if len(changed_names) > 12:
            preview += f"\n  ... ещё {len(changed_names) - 12}"

        answer = QMessageBox.question(
            self,
            APP_TITLE,
            "Записать изменённые параметры в RAM контроллера?\n\n"
            f"Изменено параметров: {len(changed_names)}\n\n"
            f"{preview}\n\n"
            "Во Flash они попадут только после SAVE Flash.",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No,
        )
        if answer != QMessageBox.Yes:
            return

        def task(log):
            client = DroneCANClient(iface, bitrate, target, log)
            written = {}
            try:
                # PCA_COUNT должен быть применён до записи новых OUT17..OUT32.
                if "PCA_COUNT" in changed:
                    value = changed["PCA_COUNT"]
                    client.write_param("PCA_COUNT", value)
                    written["PCA_COUNT"] = value
                    log(f"PCA_COUNT <- {value}")

                # Обычные системные параметры, которые не меняют адрес/скорость шины.
                if "FS_TIMEOUT" in changed:
                    value = changed["FS_TIMEOUT"]
                    client.write_param("FS_TIMEOUT", value)
                    written["FS_TIMEOUT"] = value
                    log(f"FS_TIMEOUT <- {value}")

                # Пишем только реально изменённые параметры выходов.
                active_count = int(sysv["PCA_COUNT"]) * 16
                for idx in range(1, active_count + 1):
                    for field, _lo, _hi in OUTPUT_FIELDS:
                        name = f"OUT{idx:02d}_{field}"
                        if name not in changed:
                            continue
                        value = changed[name]
                        client.write_param(name, value)
                        written[name] = value
                        log(f"{name} <- {value}")

                # NODE_ID и CAN_BITRATE потенциально требуют перезапуска,
                # поэтому оставляем их в конце операции.
                if "NODE_ID" in changed:
                    value = changed["NODE_ID"]
                    client.write_param("NODE_ID", value)
                    written["NODE_ID"] = value
                    log(f"NODE_ID <- {value} (в конце)")

                if "CAN_BITRATE" in changed:
                    value = changed["CAN_BITRATE"]
                    client.write_param("CAN_BITRATE", value)
                    written["CAN_BITRATE"] = value
                    log(f"CAN_BITRATE <- {value} (последним)")

                return written, sysv, outv, target
            finally:
                client.close()

        def done(payload):
            written, written_sysv, written_outv, old_target = payload

            # Baseline обновляем только по тем параметрам, запись которых
            # действительно завершилась успешно.
            if self.baseline is None:
                self.baseline = {}
            self.baseline.update({name: int(value) for name, value in written.items()})

            # Только успешные записи, выполненные этой GUI, считаются ожидающими SAVE.
            self.pending_flash_params.update(written.keys())
            self._update_dirty_state()

            self.append_log(
                f"В RAM записано изменённых параметров: {len(written)}."
            )
            if self.pending_flash_params:
                self.append_log("Есть изменения RAM, ещё не сохранённые во Flash.")
            text = (
                f"В RAM контроллера записано параметров: {len(written)}.\n"
                "Для постоянного хранения нажмите SAVE Flash."
            )

            if "NODE_ID" in written and written_sysv["NODE_ID"] != old_target:
                text += (
                    "\n\nNODE_ID был изменён. Поле подключения сверху оставлено "
                    f"на текущем адресе {old_target}, чтобы можно было выполнить SAVE Flash. "
                    "Новый Node ID обычно используется после перезапуска контроллера."
                )

            if "CAN_BITRATE" in written and written_sysv["CAN_BITRATE"] != bitrate:
                text += (
                    "\n\nCAN_BITRATE в конфигурации изменён. "
                    "Не меняйте локальный bitrate сверху до перезапуска контроллера."
                )

            QMessageBox.information(self, APP_TITLE, text)
            self.status.showMessage(
                f"RAM обновлена — записано параметров: {len(written)}"
            )

        self._run_worker("Запись изменений в узел...", task, done)

    def save_flash(self):
        try:
            iface, bitrate, target = self._connection_values()
        except Exception as exc:
            QMessageBox.critical(self, APP_TITLE, str(exc))
            return

        if self.baseline is None:
            QMessageBox.warning(
                self, APP_TITLE,
                "Сначала прочитайте конфигурацию узла."
            )
            return

        if not self.pending_flash_params:
            QMessageBox.information(
                self, APP_TITLE,
                "Эта GUI-сессия не записывала в RAM изменений, ожидающих SAVE Flash."
            )
            return

        answer = QMessageBox.question(
            self,
            APP_TITLE,
            "Сохранить текущую RAM-конфигурацию контроллера во Flash?",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No,
        )
        if answer != QMessageBox.Yes:
            return

        def task(log):
            client = DroneCANClient(iface, bitrate, target, log)
            try:
                ok, argument = client.execute_opcode(0)
                log(f"SAVE Flash: ok={ok} argument={argument}")
                if not ok:
                    raise RuntimeError(f"SAVE failed, argument={argument}")
                return argument
            finally:
                client.close()

        def done(argument):
            # SAVE сохраняет текущую RAM-конфигурацию. После успешного SAVE
            # больше нет изменений, которые эта GUI считает несохранёнными.
            self.pending_flash_params.clear()
            self._update_dirty_state()
            QMessageBox.information(
                self, APP_TITLE,
                f"Текущая RAM-конфигурация сохранена во Flash.\nargument={argument}"
            )
            self.append_log("Flash синхронизирован с текущей RAM-конфигурацией.")
            self.status.showMessage("SAVE Flash выполнен — RAM = Flash")

        self._run_worker("SAVE Flash...", task, done)

    def erase_flash(self):
        try:
            iface, bitrate, target = self._connection_values()
        except Exception as exc:
            QMessageBox.critical(self, APP_TITLE, str(exc))
            return

        answer = QMessageBox.warning(
            self,
            APP_TITLE,
            "ERASE удалит сохранённую конфигурацию из Flash.\n"
            "После перезапуска контроллер загрузит defaults.\n\n"
            "Продолжить?",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No,
        )
        if answer != QMessageBox.Yes:
            return

        def task(log):
            client = DroneCANClient(iface, bitrate, target, log)
            try:
                ok, argument = client.execute_opcode(1)
                log(f"ERASE Flash: ok={ok} argument={argument}")
                if not ok:
                    raise RuntimeError(f"ERASE failed, argument={argument}")
                return argument
            finally:
                client.close()

        def done(argument):
            # После ERASE GUI не гадает, что находится в Flash и откуда
            # контроллер взял текущую конфигурацию. Старые ◆ больше недостоверны.
            self.pending_flash_params.clear()
            self._update_dirty_state()

            QMessageBox.information(
                self, APP_TITLE,
                "Сохранённая Flash-конфигурация удалена.\n"
                "Текущая конфигурация будет показана такой, какой её вернёт узел."
            )
            self.append_log(
                "После ERASE состояние Flash не предполагается; "
                "через 500 мс будет автоматически прочитана фактическая RAM-конфигурация."
            )
            self.status.showMessage("ERASE Flash выполнен — ожидается чтение узла")

            # Даём контроллеру небольшой запас времени после ExecuteOpcode ERASE,
            # затем используем обычную проверенную процедуру чтения узла.
            QTimer.singleShot(500, self.read_node)

        self._run_worker("ERASE Flash...", task, done)

    # ---------- File operations ----------

    def save_file(self):
        try:
            sysv, outv = self._collect_values()
        except Exception as exc:
            QMessageBox.critical(self, APP_TITLE, str(exc))
            return

        path, _ = QFileDialog.getSaveFileName(
            self,
            "Сохранить конфигурацию",
            "dronecan_output_controller.json",
            "JSON configuration (*.json);;Все файлы (*)",
        )
        if not path:
            return
        if not path.lower().endswith(".json"):
            path += ".json"

        data = {
            "format": CONFIG_FILE_FORMAT,
            "version": CONFIG_FILE_VERSION,
            "saved_at": time.strftime("%Y-%m-%d %H:%M:%S"),
            "system": sysv,
            "outputs": {
                f"OUT{idx:02d}": vals
                for idx, vals in outv.items()
            },
        }

        try:
            with open(path, "w", encoding="utf-8") as file:
                json.dump(data, file, ensure_ascii=False, indent=2)
        except OSError as exc:
            QMessageBox.critical(
                self, APP_TITLE, f"Не удалось сохранить файл:\n{exc}"
            )
            return

        self.append_log(f"Конфигурация сохранена в файл: {path}")
        self.status.showMessage("Файл сохранён")

    def load_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Прочитать конфигурацию",
            "",
            "JSON configuration (*.json);;Все файлы (*)",
        )
        if not path:
            return

        try:
            with open(path, "r", encoding="utf-8") as file:
                data = json.load(file)

            if data.get("format") not in (
                CONFIG_FILE_FORMAT,
                "stm32_dronecan_pca9685",
            ):
                raise ValueError("Неизвестный формат файла")

            if int(data.get("version", -1)) != CONFIG_FILE_VERSION:
                raise ValueError(
                    f"Неподдерживаемая версия файла: {data.get('version')}"
                )

            sysv = {
                name: int(data["system"][name])
                for name, _lo, _hi in SYSTEM_PARAMS
            }

            count = sysv["PCA_COUNT"] * 16
            outv = {}
            for idx in range(1, count + 1):
                src = data["outputs"][f"OUT{idx:02d}"]
                outv[idx] = {
                    field: int(src[field])
                    for field, _lo, _hi in OUTPUT_FIELDS
                }

            self._validate_data(sysv, outv)

        except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
            QMessageBox.critical(
                self, APP_TITLE, f"Не удалось прочитать конфигурацию:\n{exc}"
            )
            return

        # Не меняем baseline: файл должен показывать отличия
        # относительно последнего реально прочитанного/записанного RAM узла.
        self._apply_config(sysv, outv, set_baseline=False)
        self.append_log(f"Конфигурация загружена из файла: {path}")
        self.status.showMessage("Файл загружен — ещё не записан в контроллер")

    def _validate_data(self, sysv, outv):
        for name, lo, hi in SYSTEM_PARAMS:
            value = int(sysv[name])
            if not lo <= value <= hi:
                raise ValueError(f"{name}: {value}, допустимо {lo}..{hi}")

        if int(sysv["CAN_BITRATE"]) not in CAN_BITRATES:
            raise ValueError("CAN_BITRATE: неподдерживаемое значение")

        count = int(sysv["PCA_COUNT"]) * 16
        for idx in range(1, count + 1):
            if idx not in outv:
                raise ValueError(f"Нет OUT{idx:02d}")
            for field, lo, hi in OUTPUT_FIELDS:
                value = int(outv[idx][field])
                if not lo <= value <= hi:
                    raise ValueError(
                        f"OUT{idx:02d}_{field}: {value}, допустимо {lo}..{hi}"
                    )

    # ---------- Misc ----------

    def append_log(self, text):
        stamp = time.strftime("%H:%M:%S")
        self.log.append(f"[{stamp}] {text}")

    def show_help(self):
        QMessageBox.information(
            self,
            "Помощь",
            "DroneCAN Output Controller Configurator\n\n"
            "1. Укажите CAN-интерфейс, локальный bitrate и текущий Node ID.\n"
            "2. Нажмите «Прочитать узел».\n"
            "3. Измените параметры контроллера/выходов.\n"
            "4. «Записать в узел» записывает значения только в RAM.\n"
            "5. После записи в RAM строка получает маркер ◆. После проверки нажмите SAVE Flash.\n\n"
            "TYPE:\n"
            "DISABLED — выход не управляется\n"
            "PWM — прямой PWM, failsafe берётся из FS\n"
            "ON_OFF — ON/OFF, failsafe задаёт FS STATE\n"
            "PULSE — импульс TIME, RETRIG=IGNORE/RESTART\n\n"
            "Node ID записывается последним. Изменение Node ID и CAN bitrate "
            "может потребовать перезапуска контроллера."
        )

    def show_about(self):
        QMessageBox.about(
            self,
            "О программе",
            f"{APP_TITLE}\n"
            f"{APP_REVISION}\n"
            "PySide6 + PyDroneCAN\n\n"
            "Конфигуратор DroneCAN Output Controller\n"
            "с 1–2 PCA9685 и OUT01…OUT32."
        )

    def apply_theme(self, dark):
        self.dark = bool(dark)
        app = QApplication.instance()
        app.setStyle("Fusion")

        if dark:
            app.setPalette(make_dark_palette())
            self.setStyleSheet("""
                QMainWindow { background: #252526; }
                QLabel#title { font-size: 18px; font-weight: 600; padding: 6px 0 10px 0; }
                QLabel#note, QLabel#paramDesc { color: #b8b8b8; }
                QWidget#topPanel, QWidget#controllerForm { border: 1px solid #555; border-radius: 6px; }
                QPushButton { min-height: 28px; padding: 3px 10px; }
                QLineEdit, QComboBox, QSpinBox { min-height: 26px; padding: 1px 5px; }
                QTabWidget::pane { border: 1px solid #555; }
                QTabBar::tab { min-width: 95px; padding: 7px 14px; }
                QTabBar::tab:selected { font-weight: 700; }
                QTableWidget { gridline-color: #555; }
                QHeaderView::section { padding: 6px; font-weight: 600; }
                QTableWidget QComboBox:disabled, QTableWidget QLineEdit:disabled {
                    color: #777;
                    background: #303030;
                }
            """)
        else:
            app.setPalette(app.style().standardPalette())
            self.setStyleSheet("""
                QLabel#title { font-size: 18px; font-weight: 600; padding: 6px 0 10px 0; }
                QLabel#note, QLabel#paramDesc { color: #666; }
                QWidget#topPanel, QWidget#controllerForm { border: 1px solid #bbb; border-radius: 6px; }
                QPushButton { min-height: 28px; padding: 3px 10px; }
                QLineEdit, QComboBox, QSpinBox { min-height: 26px; padding: 1px 5px; }
                QTabWidget::pane { border: 1px solid #bbb; }
                QTabBar::tab { min-width: 95px; padding: 7px 14px; }
                QTabBar::tab:selected { font-weight: 700; }
                QHeaderView::section { padding: 6px; font-weight: 600; }
                QTableWidget QComboBox:disabled, QTableWidget QLineEdit:disabled {
                    color: #999;
                    background: #e8e8e8;
                }
            """)

    def closeEvent(self, event):
        self.settings.setValue("window/geometry", self.saveGeometry())
        self.settings.setValue(
            "ui/dark_theme", self.theme_switch.isChecked()
        )
        self.settings.sync()
        super().closeEvent(event)


if __name__ == "__main__":
    app = QApplication(sys.argv)

    # Свой менеджер tooltip: каждый НОВЫЙ объект получает новую задержку 600 мс.
    tooltip_manager = DelayedToolTipManager(app)
    app.installEventFilter(tooltip_manager)

    win = MainWindow()
    win.show()

    # MainWindow создаёт много дочерних редакторов таблицы после QApplication.
    tooltip_manager.refresh_widgets()

    sys.exit(app.exec())
