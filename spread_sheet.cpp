#include "spread_sheet.h"
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QWidget>
#include <QTimer>
#include <QTime>
#include <qpainter.h>
#include <qwidget.h>
#include <qevent.h>
#include <qclipboard.h>
#include <qscrollbar>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QFileDialog>
#include <qmenu.h>
#include <qpointer.h>
#include <queue>
#include <mutex>
#include <fstream>

#include "ui_spread_sheet.h"

#ifdef _MSC_VER

#include <windows.h>

namespace
{
    int gettimeofday(struct timeval* tp, void*)
    {
        FILETIME ft;
        ::GetSystemTimeAsFileTime(&ft);
        long long t = (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        t -= 116444736000000000LL;
        t /= 10; // microseconds
        tp->tv_sec = static_cast<long>(t / 1000000UL);
        tp->tv_usec = static_cast<long>(t % 1000000UL);
        return 0;
    }

    class Timer
    {
    public:
        Timer()
        {
            gettimeofday(&start_, NULL);
        }

        // second
        double Elapsed()
        {
            struct timeval end;
            gettimeofday(&end, NULL);
            double interval = (end.tv_sec + (double)end.tv_usec * 1.0e-6) - (start_.tv_sec + (double)start_.tv_usec * 1.0e-6);
            return interval;
        }

    private:
        struct timeval start_;

    };

}

#endif


namespace tool
{
    class SpreadSheet::Internal
    {
    public:
        Ui::SpreadSheet Ui;
        std::queue<DatasPtr> data_;
        std::mutex lock_;
        std::vector<int> idxs_;// poi indexs
        bool need_stop_;
        DatasPtr current_;
        bool need_reorder_;
        int order_column_;
        bool roi_mode_;

        int visiable_first = 0;
        int visiable_last = -1;
        int rows_to_show = 200;

        Internal(SpreadSheet* self):
            need_stop_(false),
            need_reorder_(false),
            roi_mode_(false),
            order_column_(0)
        {
            this->Ui.setupUi(self);
            this->Ui.tableView->setContextMenuPolicy(Qt::CustomContextMenu);
        }

        ~Internal() {}
    };

    SpreadSheet::SpreadSheet(int row, int col, QWidget *parent)
        :QWidget(parent)
        , row_(row)
        , column_(col)
        , Internals(new SpreadSheet::Internal(this))
    {
        this->dataTable = this->Internals->Ui.tableView;
        QStandardItemModel* tableModel = new QStandardItemModel(this);

        QStringList title;
        for (int i = 0; i < this->column_; i++)
        {
            QString col_str;
            col_str = col_str.sprintf("col %d", i);
            title.push_back(col_str);
        }
        tableModel->setHorizontalHeaderLabels(title);
        dataTable->horizontalHeader()->setMinimumHeight(25);
        dataTable->horizontalHeader()->setStyleSheet("QHeaderView::section {"
            "color: black;padding-left: 4px;border: 1px solid gray;}");//border: 1px solid #6c6c6c;
        dataTable->verticalHeader()->setStyleSheet("QHeaderView::section {"
            "color: black;padding-left: 4px;border: 1px solid gray;}");//border: 1px solid #6c6c6c;


        for (int r = 0; r < row; ++r) {
            for (int c = 0; c < column_; ++c) {
                QStandardItem *item = NULL;

                if (c == 0)
                {
                    QVariant v = r;
                    item = new QStandardItem();
                    item->setData(v, Qt::EditRole);
                }
                else
                    item = new QStandardItem(QString("..."));// default is ...
                tableModel->setItem(r, c, item);
            }
        }
        this->Internals->visiable_first = -1;
        this->Internals->visiable_last = -1;

        this->dataTable->setEditTriggers(QAbstractItemView::NoEditTriggers);// read only
        this->dataTable->setModel(tableModel);
        this->dataTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

        this->dataTable->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
        this->dataTable->horizontalHeader()->setSortIndicatorShown(true);
        this->dataTable->setContextMenuPolicy(Qt::CustomContextMenu);
        dataTable->setSortingEnabled(false);// disable default order

        // right click menu
        this->right_popup_menu_ = new QMenu(this);
        this->action_select_col_ = new QAction(tr("Select Column"), this);
        this->action_select_all_ = new QAction(tr("Select All"), this);
        this->action_copy_ = new QAction(tr("Copy"), this);
        this->action_export_ = new QAction(tr("Export"), this);

        this->right_popup_menu_->addAction(action_select_col_);
        this->right_popup_menu_->addAction(action_select_all_);
        this->right_popup_menu_->addAction(action_copy_);
        this->right_popup_menu_->addAction(action_export_);

        // table range changed
        connect(this->dataTable, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(onCustomContextMenuRequested(const QPoint &)));

        connect(this->action_select_col_, SIGNAL(triggered()), this, SLOT(onActionSelectColumn()));
        connect(this->action_select_all_, SIGNAL(triggered()), this, SLOT(onActionSelectAll()));
        connect(this->action_copy_, SIGNAL(triggered()), this, SLOT(onActionCopy()));
        connect(this->action_export_, SIGNAL(triggered()), this, SLOT(onActionExport()));

        connect(this, SIGNAL(tableUpdate()), this, SLOT(slotUpdate()));

        // vertical scrollbar valuechanged
        QScrollBar *bar = this->dataTable->verticalScrollBar();
        connect((QWidget*)bar, SIGNAL(valueChanged(int)), this, SLOT(verticalScrollMoved(int)));
        connect(this->dataTable->horizontalHeader(), SIGNAL(sortIndicatorChanged(int, Qt::SortOrder)), this, SLOT(sortIndicatorChanged(int, Qt::SortOrder)));
        refresh_task_.reset(new std::thread(std::bind(&SpreadSheet::updateThread, this)));

        // init status: hide all items
        this->dataTable->setUpdatesEnabled(false);
        for (int r = row - 1; r >= 0; --r)
            dataTable->setRowHidden(r, true);
        this->dataTable->setUpdatesEnabled(true);

    }

    SpreadSheet::~SpreadSheet()
    {
        if (refresh_task_)
        {
            this->Internals->need_stop_ = true;
            refresh_task_->join();
            refresh_task_.reset();
        }
    }

    void SpreadSheet::updateThread()
    {
        while (!this->Internals->need_stop_)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(20));

            std::lock_guard<std::mutex> lock(this->Internals->lock_);
            {
                if (!this->Internals->data_.size())
                    continue;
                this->Internals->current_ = this->Internals->data_.front();
                this->Internals->data_.pop();
                while (this->Internals->data_.size() > 2)
                    this->Internals->data_.pop();
                emit tableUpdate();
            }
        }
    }

    void SpreadSheet::adjustRows(int rows)
    {
        QStandardItemModel* tableModel = (QStandardItemModel*)this->dataTable->model();
        int row_size = tableModel->rowCount();
        int col_size = tableModel->columnCount();

        // add rows
        if (row_size > rows)
        {
            int add = rows - row_size;// need to add new rows
            for (int r = row_size; r < rows; ++r) {
                for (int c = 0; c < col_size; ++c) {
                    QStandardItem *item = NULL;

                    if (c == 0)
                    {
                        QVariant v = r;
                        item = new QStandardItem();
                        item->setData(v, Qt::EditRole);
                    }
                    else
                        item = new QStandardItem(QString("..."));
                    tableModel->setItem(r, c, item);
                }
            }
        }

        // hide rows
        if (rows != this->data_rows_)
        {
            this->dataTable->setUpdatesEnabled(false);
            if (rows > this->data_rows_)// show more rows
            {
                for (int r = this->data_rows_; r < rows; ++r)
                    this->dataTable->setRowHidden(r, false);
            }
            else if (rows < this->data_rows_)// hide more rows
            {
                for (int r = this->data_rows_ - 1; r >= rows; --r)
                    this->dataTable->setRowHidden(r, true);
            }
            else
            {

            }
            this->dataTable->setUpdatesEnabled(true);
            this->data_rows_ = rows;
        }
    }

    void SpreadSheet::getVisiableRow(int& first, int& last)
    {
        first = this->Internals->Ui.tableView->rowAt(0);// visiable first row 
        last = this->Internals->Ui.tableView->rowAt(this->Internals->Ui.tableView->viewport()->rect().bottom());// visiable last row
    }

    void SpreadSheet::Update(DatasPtr& data)
    {
        {
            std::lock_guard<std::mutex> lock(this->Internals->lock_);
            this->Internals->data_.push(data);
        }
    }

    void SpreadSheet::reject()
    {
        //QWidget::reject();
    }

    void SpreadSheet::closeEvent(QCloseEvent *event)
    {
        //QDialog::closeEvent(event);
    }


    //Ctrl + C copy
    bool SpreadSheet::event(QEvent *event)
    {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->matches(QKeySequence::Copy)) {
                QString copied_text;

                //QAbstractItemModel * model = this->pointTable->model();
                QItemSelectionModel * selection = this->dataTable->selectionModel();
                QModelIndexList indexes = selection->selectedIndexes();
                if (!indexes.size())
                {
                    event->accept();
                    return true;
                }
                int current_row = indexes.at(0).row();
                for (int i = 0; i < indexes.count(); i++) {
                    if (current_row != indexes.at(i).row()) {
                        current_row = indexes.at(i).row();
                        copied_text.append("\n");
                        copied_text.append(indexes.at(i).data().toString());
                        continue;
                    }
                    if (0 != i) {
                        copied_text.append("\t");
                    }
                    copied_text.append(indexes.at(i).data().toString());
                }
                copied_text.append("\n");
                QClipboard *clb = QApplication::clipboard();
                clb->setText(copied_text);
                event->accept();
                return true;
            }
        }
        return QWidget::event(event);
    }

    void SpreadSheet::updatePoiRegion(std::vector<int>& indexs, bool roi_mode)
    {
        // release lock before emit, resource busy
        {
            std::lock_guard<std::mutex> lock(this->Internals->lock_);
            this->Internals->idxs_ = indexs;
            this->Internals->roi_mode_ = roi_mode;
        }
        emit tableUpdate();
    }

    template <typename T>
    void sort_indexes(const std::vector<T>& v, std::vector<size_t>& idx)
    {

        // initialize original index locations
        iota(idx.begin(), idx.end(), 0);

        // sort indexes based on comparing values in v
        // using std::stable_sort instead of std::sort
        // to avoid unnecessary index re-orderings
        // when v contains elements of equal values 
        std::stable_sort(idx.begin(), idx.end(),
            [&v](size_t i1, size_t i2) {return v[i1] < v[i2]; });

        return;
    }

    // http://www.cplusplus.com/forum/beginner/116101/
    template <typename Container>
    struct compare_indirect_index
    {
        const Container& container;
        compare_indirect_index(const Container& container) : container(container) { }
        bool operator () (size_t lindex, size_t rindex) const
        {
            return container[lindex] < container[rindex];
        }
    };

    template <typename Container>
    void sort_data(Container& v, std::vector<size_t>& idx, bool is_ascend = true)
    {
        // initialize original index locations
        iota(idx.begin(), idx.end(), 0);

        // sort indexes based on comparing values in v
        // using std::stable_sort instead of std::sort
        // to avoid unnecessary index re-orderings
        // when v contains elements of equal values
        std::stable_sort(idx.begin(), idx.end(), compare_indirect_index <decltype(v)>(v));
        if (!is_ascend)
        {
            std::reverse(idx.begin(), idx.end());
        }
        return;
    }

    void SpreadSheet::slotUpdate()
    {
        QStandardItemModel* tableModel = (QStandardItemModel*)this->dataTable->model();
        DatasPtr data_ori = NULL;
        DatasPtr data = NULL;
        {
            std::lock_guard<std::mutex> lock(this->Internals->lock_);
            data_ori = this->Internals->current_;
            if (!data_ori)
                return;

            if (this->Internals->roi_mode_)
            {
                DatasPtr roi(new Datas);
                DatasPtr full = data_ori;

                roi->resize(this->Internals->idxs_.size());
                int it = 0;
                for (auto& id : this->Internals->idxs_)
                {
                    DataStruct& p_roi = roi->at(it);
                    DataStruct& p_full = full->at(id);
                    p_roi = p_full;
                    it++;
                }
                data = roi;
            }
            else
            {
                data = data_ori;
            }
        }

        if (!data)
            return;

        // sort data
        int sort_column = this->dataTable->horizontalHeader()->sortIndicatorSection();
        std::vector<size_t> index(data->size());
        std::iota(std::begin(index), std::end(index), 0); // fill index with {0,1,2,...} This only needs to happen once
        bool is_ascend = true;
        if (Qt::SortOrder::AscendingOrder != this->dataTable->horizontalHeader()->sortIndicatorOrder())// 0 is AscendingOrder, 1 is DescendingOrder
            is_ascend = false;
        if (1)
        {
            if (sort_column > 2)// float
            {
                std::vector<double> values(data->size());
                for (int i = 0; i < data->size();i++)
                {
                    DataStruct& it = data->at(i);
                    float v = .0f;
                    switch (sort_column)
                    {
                    case 3:
                        v = it.v3;
                        break;
                    default:
                        break;
                    }
                    values[i] = v;
                }
                sort_data(values, index, is_ascend);
            }
            else if ((sort_column >= 0) && (sort_column <= 2))
            {
                std::vector<int> values(data->size());
                for (int i = 0; i < data->size(); i++)
                {
                    DataStruct& it = data->at(i);
                    int v = 0;
                    switch (sort_column)
                    {
                    case 0:
                        v = it.idx;
                        break;
                    case 1:
                        v = it.v1;
                        break;
                    case 2:
                        v = it.v2;
                        break;
                    case 3:
                        v = it.v3;
                        break;
                    default:
                        break;
                    }
                    values[i] = v;
                }
                sort_data(values, index, is_ascend);
            }
            else
            {
                return;
            }
        }
        int new_size = data->size();
        adjustRows(new_size);
        if (new_size <= 0)
            return;

        int visible_first = -1;
        int visible_last = -1;
        getVisiableRow(visible_first, visible_last);
        if (-1 == visible_last)// if data columns is less than the view columns, show all data
            visible_last = new_size;

        this->dataTable->setUpdatesEnabled(false);
        for (int r = visible_first; r <= visible_last; ++r) {
            if (r >= new_size)
                continue;
            int rr = index[r];
            if (rr >= new_size)
                continue;

            DataStruct& v = data->at(rr);
            tableModel->item(r, 0)->setData(v.idx, Qt::EditRole);
            tableModel->item(r, 1)->setData(v.v1, Qt::EditRole);
            tableModel->item(r, 2)->setData(v.v2, Qt::EditRole);
            tableModel->item(r, 3)->setData(int(v.v3 * 1000) / 1000.0, Qt::EditRole);
        }
        this->dataTable->setUpdatesEnabled(true);
        return;
    }

    void SpreadSheet::onCustomContextMenuRequested(const QPoint &pos)
    {
        QItemSelectionModel * selection = this->dataTable->selectionModel();
        QModelIndexList indexes = selection->selectedIndexes();

        if (!indexes.size())
        {
            this->action_copy_->setDisabled(true);
        }
        else
        {
            this->action_copy_->setDisabled(false);
        }

        if (1 == indexes.size())
        {
            this->action_select_col_->setDisabled(false);
        }
        else
        {
            this->action_select_col_->setDisabled(true);
        }


        this->right_popup_menu_->exec(QCursor::pos());
    }

    void SpreadSheet::onActionSelectColumn()
    {
        QItemSelectionModel * selection = this->dataTable->selectionModel();
        QModelIndexList indexes = selection->selectedIndexes();
        if (1 == indexes.size())
        {
            this->dataTable->selectColumn(indexes.at(0).column());
        }
    }

    /*select all*/
    void SpreadSheet::onActionSelectAll()
    {
        this->dataTable->selectAll();
    }

    /*copy*/
    void SpreadSheet::onActionCopy()
    {
        QString copied_text;
        QItemSelectionModel * selection = this->dataTable->selectionModel();
        QModelIndexList indexes = selection->selectedIndexes();
        QModelIndex index;

        if (!indexes.size())
        {
            return;
        }

        int current_row = indexes.at(0).row();
        for (int i = 0; i < indexes.count(); i++) {
            if (i == 0)
            {
                copied_text.append("\n");
            }
            if (current_row != indexes.at(i).row()) {
                current_row = indexes.at(i).row();
                if (!this->dataTable->isRowHidden(current_row))
                {
                    copied_text.append("\n");
                    copied_text.append(indexes.at(i).data().toString());
                }
                continue;
            }
            if (0 != i) {
                copied_text.append("\t");
            }

            if (!this->dataTable->isRowHidden(indexes.at(i).row()))
            {
                copied_text.append(indexes.at(i).data().toString());
            }
        }
        copied_text.append("\n");
        QClipboard *clb = QApplication::clipboard();
        QString originalText = clb->text();
        clb->setText(copied_text);
    }

    void SpreadSheet::onActionExport()
    {
        QString filename = QFileDialog::getSaveFileName(this, "Save to file");
        if (!filename.size())
            return;

        DatasPtr data_ori = NULL;
        DatasPtr data = NULL;
        {
            std::lock_guard<std::mutex> lock(this->Internals->lock_);
            data_ori = this->Internals->current_;
            if (!data_ori)
                return;

            if (this->Internals->roi_mode_)
            {
                DatasPtr roi(new Datas());
                DatasPtr full = data_ori;

                roi->resize(this->Internals->idxs_.size());
                int it = 0;
                for (auto& id : this->Internals->idxs_)
                {
                    DataStruct& p_roi = roi->at(it);
                    DataStruct& p_full = full->at(id);
                    p_roi = p_full;
                    it++;
                }
                data = roi;
            }
            else
            {
                data = data_ori;
            }
        }

        if (!data)
            return;

        // sort data
        int sort_column = this->dataTable->horizontalHeader()->sortIndicatorSection();
        std::vector<size_t> index(data->size());
        std::iota(std::begin(index), std::end(index), 0); // fill index with {0,1,2,...} This only needs to happen once
        bool is_ascend = true;
        if (Qt::SortOrder::AscendingOrder != this->dataTable->horizontalHeader()->sortIndicatorOrder())// 0 is AscendingOrder, 1 is DescendingOrder
            is_ascend = false;
        if (/*this->Internals->need_reorder_ && */1)
        {
            //this->Internals->need_reorder_ = false;
            if (sort_column > 2)// float
            {
                std::vector<double> values(data->size());
                for (int i = 0; i < data->size(); i++)
                {
                    DataStruct& it = data->at(i);
                    float v = .0f;
                    switch (sort_column)
                    {
                    case 3:
                        v = it.v3;
                        break;
                    default:
                        break;
                    }
                    values[i] = v;
                }
                sort_data(values, index, is_ascend);
            }
            else if ((sort_column >= 0) && (sort_column <= 2))
            {
                std::vector<int> values(data->size());
                for (int i = 0; i < data->size(); i++)
                {
                    DataStruct& it = data->at(i);
                    int v = 0;
                    switch (sort_column)
                    {
                    case 0:
                        v = it.idx;
                        break;
                    case 1:
                        v = it.v1;
                        break;
                    case 2:
                        v = it.v2;
                        break;
                    case 3:
                        v = it.v3;
                        break;
                    default:
                        break;
                    }
                    values[i] = v;
                }
                sort_data(values, index, is_ascend);
            }
            else
            {
                return;
            }
        }
        int new_size = data->size();
        if (new_size <= 0)
            return;

        std::string name = filename.toLocal8Bit().toStdString();
        std::ofstream out(name);
        if (!out.is_open())
        {
            QMessageBox::warning(this, "Warning", "Open file error.");
            return;
        }

        const int buffer_len = 1024;
        std::unique_ptr<char> buffer(new char[buffer_len]);
        char* ptr = buffer.get();
        for (int r = 0; r < new_size; ++r) {
            if (r >= index.size())
                continue;
            int rr = index[r];
            if (rr >= new_size)
                continue;

            int pos = 0;
            DataStruct& v = data->at(rr);
            pos += snprintf(ptr + pos, buffer_len - pos, "%d %d %d %.3f", v.idx, v.v1, v.v2, v.v3);
            out << std::string(ptr);
        }
        out.close();
        return;
    }

    void SpreadSheet::verticalScrollMoved(int value)
    {
        emit tableUpdate();
    }

    void SpreadSheet::headerClicked(int value)//header is clicked
    {
        this->dataTable->horizontalHeader()->sortIndicatorOrder();
    }

    void SpreadSheet::sortIndicatorChanged(int logicalindex, Qt::SortOrder order)//order indicator changded
    {
        static int order_col = 0;// record the last order column number
        this->order_ = order;
        this->sort_column_ = logicalindex;
        QString ascend_label = " ^";
        QString descend_label = " v";
        QStandardItemModel* tableModel = (QStandardItemModel*)this->dataTable->horizontalHeader()->model();
        QString title = "";
        QString new_title = title;

        // remove the old sort title order label(^ or v)
        title = this->dataTable->model()->headerData(order_col, Qt::Horizontal).toString();
        if (order_col != logicalindex)
        {
            if (title.endsWith(ascend_label) || title.endsWith(descend_label))
            {
                new_title = title.mid(0, title.size() - 2);
                this->dataTable->model()->setHeaderData(order_col, Qt::Horizontal, new_title);
            }
        }

        // set new sort title title order label(^ or v)
        title = this->dataTable->model()->headerData(logicalindex, Qt::Horizontal).toString();
        new_title = title;
        if (title.endsWith(ascend_label) || title.endsWith(descend_label))
        {
            new_title = title.mid(0, title.size() - 2);
        }
        if (Qt::SortOrder::AscendingOrder == order)
            new_title += QString(ascend_label);
        else if (Qt::SortOrder::DescendingOrder == order)
            new_title += QString(descend_label);
        else
            ;
        this->dataTable->model()->setHeaderData(logicalindex, Qt::Horizontal, new_title);
        order_col = logicalindex;
        emit tableUpdate();
    }
}