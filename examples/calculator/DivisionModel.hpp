#pragma once

#include "DecimalData.hpp"
#include "MathOperationDataModel.hpp"

#include <QtNodes/NodeDelegateModel>

#include <QtCore/QObject>
#include <QtWidgets/QLabel>

/// The model dictates the number of inputs and outputs for the Node.
/// In this example it has no logic.
class DivisionModel : public MathOperationDataModel
{
public:
    virtual ~DivisionModel() {}

public:
    QString caption() const override { return QStringLiteral("Division"); }

    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override
    {
        Q_UNUSED(portType);
        Q_UNUSED(portIndex);
        return true;
    }

    QString portCaption(PortType portType, PortIndex portIndex) const override
    {
        switch (portType) {
        case PortType::In:
            if (portIndex == 0)
                return QStringLiteral("Dividend");
            else if (portIndex == 1)
                return QStringLiteral("Divisor");

            break;

        case PortType::Out:
            return QStringLiteral("Result");

        default:
            break;
        }
        return QString();
    }

    QString name() const override { return QStringLiteral("Division"); }

private:
    void compute() override
    {
        PortIndex const outPortIndex = 0;

        auto n1 = _number1.lock();
        auto n2 = _number2.lock();

        if (n2 && (n2->number() == 0.0)) {
            setValidationState(QtNodes::NodeValidationState(
                QtNodes::NodeValidationState::State::Error,
                QStringLiteral("Division by zero error")));
            _result.reset();
        } else if (n2 && (n2->number() < 1e-5)) {
            setValidationState(QtNodes::NodeValidationState(
                QtNodes::NodeValidationState::State::Warning,
                QStringLiteral("Very small divident. Result might overflow")));
            if (n1) {
                _result = std::make_shared<DecimalData>(n1->number() / n2->number());
            } else {
                _result.reset();
            }
        } else if (n1 && n2) {
            setValidationState(QtNodes::NodeValidationState());
            _result = std::make_shared<DecimalData>(n1->number() / n2->number());
        } else {
            setValidationState(QtNodes::NodeValidationState());
            _result.reset();
        }

        Q_EMIT dataUpdated(outPortIndex);
    }
};
