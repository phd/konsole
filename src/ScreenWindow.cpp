/*
    SPDX-FileCopyrightText: 2007 Robert Knight <robertknight@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ScreenWindow.h"

// Konsole

using namespace Konsole;

ScreenWindow::ScreenWindow(Screen *screen, QObject *parent)
    : QObject(parent)
    , _screen(nullptr)
    , _windowBuffer(nullptr)
    , _windowBufferSize(0)
    , _bufferNeedsUpdate(true)
    , _windowLines(1)
    , _currentLine(0)
    , _currentResultLine(-1)
    , _trackOutput(true)
    , _scrollCount(0)
{
    setScreen(screen);
}

ScreenWindow::~ScreenWindow()
{
    delete[] _windowBuffer;
}

void ScreenWindow::setScreen(Screen *screen)
{
    Q_ASSERT(screen);

    if (screen == _screen) {
        return;
    }

    Q_EMIT screenAboutToChange();
    _screen = screen;
}

Screen *ScreenWindow::screen() const
{
    return _screen;
}

Character *ScreenWindow::getImage()
{
    // reallocate internal buffer if the window size has changed
    int size = windowLines() * windowColumns();
    if (_windowBuffer == nullptr || _windowBufferSize != size) {
        delete[] _windowBuffer;
        _windowBufferSize = size;
        _windowBuffer = new Character[size];
        _bufferNeedsUpdate = true;
    }

    if (!_bufferNeedsUpdate) {
        return _windowBuffer;
    }

    _screen->getImage(_windowBuffer, size, currentLine(), endWindowLine());

    // this window may look beyond the end of the screen, in which
    // case there will be an unused area which needs to be filled
    // with blank characters
    fillUnusedArea();

    _bufferNeedsUpdate = false;
    return _windowBuffer;
}

void ScreenWindow::fillUnusedArea()
{
    int screenEndLine = _screen->getHistLines() + _screen->getLines() - 1;
    int windowEndLine = currentLine() + windowLines() - 1;

    int unusedLines = windowEndLine - screenEndLine;

    // stop when unusedLines is negative; there is an issue w/ charsToFill
    //  being greater than an int can hold
    if (unusedLines <= 0) {
        return;
    }

    int charsToFill = unusedLines * windowColumns();

    Screen::fillWithDefaultChar(_windowBuffer + _windowBufferSize - charsToFill, charsToFill);
}

// return the index of the line at the end of this window, or if this window
// goes beyond the end of the screen, the index of the line at the end
// of the screen.
//
// when passing a line number to a Screen method, the line number should
// never be more than endWindowLine()
//
int ScreenWindow::endWindowLine() const
{
    return qMin(currentLine() + windowLines() - 1, lineCount() - 1);
}

QVector<LineProperty> ScreenWindow::getLineProperties()
{
    QVector<LineProperty> result = _screen->getLineProperties(currentLine(), endWindowLine());

    if (result.count() != windowLines()) {
        result.resize(windowLines());
    }

    return result;
}

QString ScreenWindow::selectedText(const Screen::DecodingOptions options) const
{
    return _screen->selectedText(options);
}

void ScreenWindow::getSelectionStart(int &column, int &line)
{
    _screen->getSelectionStart(column, line);
    line -= currentLine();
}

void ScreenWindow::getSelectionEnd(int &column, int &line)
{
    _screen->getSelectionEnd(column, line);
    line -= currentLine();
}

void ScreenWindow::setSelectionStart(int column, int line, bool columnMode)
{
    _screen->setSelectionStart(column, line + currentLine(), columnMode);

    _bufferNeedsUpdate = true;
    Q_EMIT selectionChanged();
}

void ScreenWindow::setSelectionEnd(int column, int line, bool trimTrailingWhitespace)
{
    _screen->setSelectionEnd(column, line + currentLine(), trimTrailingWhitespace);

    _bufferNeedsUpdate = true;
    Q_EMIT selectionChanged();
}

void ScreenWindow::setSelectionByLineRange(int start, int end)
{
    clearSelection();

    _screen->setSelectionStart(0, start, false);
    _screen->setSelectionEnd(windowColumns(), end, false);

    _bufferNeedsUpdate = true;
    Q_EMIT selectionChanged();
}

bool ScreenWindow::isSelected(int column, int line)
{
    return _screen->isSelected(column, qMin(line + currentLine(), endWindowLine()));
}

void ScreenWindow::clearSelection()
{
    _screen->clearSelection();

    Q_EMIT selectionChanged();
}

void ScreenWindow::setWindowLines(int lines)
{
    Q_ASSERT(lines > 0);
    _windowLines = lines;
}

int ScreenWindow::windowLines() const
{
    return _windowLines;
}

int ScreenWindow::windowColumns() const
{
    return _screen->getColumns();
}

int ScreenWindow::lineCount() const
{
    return _screen->getHistLines() + _screen->getLines();
}

int ScreenWindow::columnCount() const
{
    return _screen->getColumns();
}

QPoint ScreenWindow::cursorPosition() const
{
    QPoint position;

    position.setX(_screen->getCursorX());
    position.setY(_screen->getCursorY());

    return position;
}

int ScreenWindow::currentLine() const
{
    return qBound(0, _currentLine, std::max(0, lineCount() - windowLines()));
}

int ScreenWindow::currentResultLine() const
{
    return _currentResultLine;
}

void ScreenWindow::setCurrentResultLine(int line)
{
    if (_currentResultLine == line) {
        return;
    }

    _currentResultLine = line;
    Q_EMIT currentResultLineChanged();
}

void ScreenWindow::scrollBy(RelativeScrollMode mode, int amount, bool fullPage)
{
    if (mode == ScrollLines) {
        scrollTo(currentLine() + amount);
    } else if (mode == ScrollPages || (mode == ScrollPrompts && !_screen->hasRepl())) {
        if (fullPage) {
            scrollTo(currentLine() + amount * (windowLines()));
        } else {
            scrollTo(currentLine() + amount * (windowLines() / 2));
        }
    } else if (mode == ScrollPrompts) {
        int i = currentLine();
        if (amount < 0) {
            QVector<LineProperty> properties = _screen->getLineProperties(0, currentLine());
            while (i > 0 && amount < 0) {
                i--;
                if ((properties[i].flags.f.prompt_start) != 0) {
                    if (++amount == 0) {
                        break;
                    }
                }
            }
        } else if (amount > 0) {
            QVector<LineProperty> properties = _screen->getLineProperties(currentLine(), _screen->getHistLines());
            while (i < _screen->getHistLines() && amount > 0) {
                i++;
                if ((properties[i - currentLine()].flags.f.prompt_start) != 0) {
                    if (--amount == 0) {
                        break;
                    }
                }
            }
        }
        scrollTo(i);
    }
}

bool ScreenWindow::atEndOfOutput() const
{
    return currentLine() == (lineCount() - windowLines());
}

void ScreenWindow::scrollTo(int line)
{
    int maxCurrentLineNumber = lineCount() - windowLines();
    line = qBound(0, line, maxCurrentLineNumber);

    const int delta = line - _currentLine;
    _currentLine = line;

    // keep track of number of lines scrolled by,
    // this can be reset by calling resetScrollCount()
    _scrollCount += delta;

    _bufferNeedsUpdate = true;

    Q_EMIT scrolled(_currentLine);
}

void ScreenWindow::setTrackOutput(bool trackOutput)
{
    _trackOutput = trackOutput;
}

bool ScreenWindow::trackOutput() const
{
    return _trackOutput;
}

int ScreenWindow::scrollCount() const
{
    return _scrollCount;
}

void ScreenWindow::resetScrollCount()
{
    _scrollCount = 0;
}

QRect ScreenWindow::scrollRegion() const
{
    bool equalToScreenSize = windowLines() == _screen->getLines();

    if (atEndOfOutput() && equalToScreenSize) {
        return _screen->lastScrolledRegion();
    }
    return {0, 0, windowColumns(), windowLines()};
}

void ScreenWindow::updateCurrentLine()
{
    if (!_screen->isResize()) {
        return;
    }
    if (_currentLine > 0) {
        _currentLine -= _screen->getOldTotalLines() - lineCount();
    }
    _currentLine = qBound(0, _currentLine, lineCount() - windowLines());
}

void ScreenWindow::notifyOutputChanged()
{
    // move window to the bottom of the screen and update scroll count
    // if this window is currently tracking the bottom of the screen
    if (_trackOutput) {
        _scrollCount -= _screen->scrolledLines();
        _currentLine = qMax(0, _screen->getHistLines() - (windowLines() - _screen->getLines()));
    } else {
        // if the history is not unlimited then it may
        // have run out of space and dropped the oldest
        // lines of output - in this case the screen
        // window's current line number will need to
        // be adjusted - otherwise the output will scroll
        _currentLine = qMax(0, _currentLine - _screen->droppedLines());

        // ensure that the screen window's current position does
        // not go beyond the bottom of the screen
        _currentLine = qMin(_currentLine, _screen->getHistLines());
    }

    _bufferNeedsUpdate = true;

    Q_EMIT outputChanged();
}

#include "moc_ScreenWindow.cpp"
