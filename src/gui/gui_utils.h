#pragma once

#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <oclero/qlementine/utils/IconUtils.hpp>
#include <oclero/qlementine/icons/Icons16.hpp>
#include <QIcon>
#include <QSize>

namespace grn {

inline QIcon makeThemedIcon(oclero::qlementine::icons::Icons16 id, const QSize& size = { 16, 16 }) {
    const auto svgPath = oclero::qlementine::icons::iconPath(id);
    if (auto* style = oclero::qlementine::appStyle()) {
        return style->makeThemedIcon(svgPath, size);
    } else {
        return QIcon(svgPath);
    }
}

} // namespace grn
