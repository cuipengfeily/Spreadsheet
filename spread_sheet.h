#ifndef SPREAD_SHEET_H
#define SPREAD_SHEET_H

#include <QWidget>
#include <QTableView>
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QPair>
#include <QSet>
#include <thread>

class QItemSelection;

namespace tool
{
    typedef struct DataStruct {
        int idx = 0;
        int v1 = 0;
        int v2 = 0;
        float v3 = 0.0;
    }DataStruct;

    using Datas = std::vector<DataStruct>;
    using DatasPtr = std::shared_ptr<Datas>;

    class SpreadSheet : public QWidget
    {
        Q_OBJECT

    public:
        SpreadSheet(int row, int col, QWidget* parent = 0);
        ~SpreadSheet();

        virtual bool event(QEvent *e);

        //update the indexs which are interested
        void updatePoiRegion(std::vector<int>& indexs, bool roi_mode = true);

        public slots:
        virtual	void	reject();

        void Update(DatasPtr&);

    protected:

        virtual void closeEvent(QCloseEvent *event);

        // update datas
        void updateThread();

        // adjust rows
        void adjustRows(int row);

        // get the visiable row range
        void getVisiableRow(int& first, int& last);

        private slots :

        void slotUpdate();

        /*right button menu*/
        void onCustomContextMenuRequested(const QPoint &pos);

        /*colume selected action*/
        void onActionSelectColumn();

        /*select all action*/
        void onActionSelectAll();

        /*copy to */
        void onActionCopy();

        /*export data to file*/
        void onActionExport();

        // not used
        void verticalScrollMoved(int);

        // not used
        void headerClicked(int);

        // not used
        void sortIndicatorChanged(int, Qt::SortOrder);

    signals:

        void tableUpdate();

    private:

        QTableView* dataTable;
        QMenu *right_popup_menu_;

        QAction *action_select_col_;//select column action
        QAction *action_select_all_;//select all action
        QAction *action_copy_;//copy action
        QAction *action_export_;//export data action

        int sort_column_; // not used
        Qt::SortOrder order_; // not used

        std::shared_ptr<std::thread> refresh_task_;

        int data_rows_ = 0;//not hide
        int data_columns_ = 0;//not hide
        
        int visiable_first_row_ = -1;//visiable
        int visiable_last_row_ = -1;//visiable

        int row_ = 102400; // buffer enough
        int column_ = 11; // column

        class Internal;
        Internal* Internals;
        friend class Internal;

    };
}

#endif // SPREAD_SHEET_H
