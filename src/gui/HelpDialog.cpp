#include "HelpDialog.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace kubik {

void HelpDialog::show(QWidget* parent, const QString& title, const QString& html) {
    auto* dlg = new QDialog(parent);
    dlg->setWindowTitle(title);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(dlg);
    auto* browser = new QTextBrowser(dlg);
    browser->setOpenExternalLinks(true);
    browser->setHtml(html);
    layout->addWidget(browser, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    layout->addWidget(buttons);

    dlg->resize(560, 480);
    dlg->show();
}

}  // namespace kubik
