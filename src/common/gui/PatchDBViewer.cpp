//
// Created by Paul Walker on 4/15/21.
//

#include "PatchDBViewer.h"
#include "PatchDB.h"
#include "SurgeGUIEditor.h"

class PatchDBSQLTableModel : public juce::TableListBoxModel
{
  public:
    PatchDBSQLTableModel(SurgeGUIEditor *ed, SurgeStorage *s) : editor(ed), storage(s) {}
    int getNumRows() override { return data.size(); }

    void paintRowBackground(juce::Graphics &g, int rowNumber, int width, int height,
                            bool rowIsSelected) override
    {
        // this is obviously a dumb hack
        if (rowNumber % 2 == 0)
            g.fillAll(juce::Colour(170, 170, 200));
        else
            g.fillAll(juce::Colour(190, 190, 190));
    }

    void paintCell(juce::Graphics &g, int rowNumber, int columnId, int width, int height,
                   bool rowIsSelected) override
    {
        g.setColour(juce::Colour(100, 100, 100));
        g.drawRect(juce::Rectangle<int>{0, 0, width - 1, height - 1});
        g.setColour(juce::Colour(0, 0, 0));
        auto d = data[rowNumber];
        auto s = std::to_string(d.id);
        switch (columnId)
        {
        case 2:
            s = d.name;
            break;
        case 3:
            s = d.cat;
            break;
        case 4:
            s = d.author;
            break;
        }
        g.drawText(s.c_str(), 0, 0, width, height, juce::Justification::centredLeft);
    }

    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent &event) override
    {
        auto d = data[rowNumber];
        editor->queuePatchFileLoad(d.file);
        editor->closePatchBrowserDialog();
    }
    void executeQuery(const std::string &n) { data = storage->patchDB->rawQueryForNameLike(n); }
    std::vector<Surge::PatchStorage::PatchDB::record> data;
    SurgeStorage *storage;
    SurgeGUIEditor *editor;
};

PatchDBViewer::PatchDBViewer(SurgeGUIEditor *e, SurgeStorage *s)
    : editor(e), storage(s), juce::Component("PatchDB Viewer")
{
    createElements();
}
PatchDBViewer::~PatchDBViewer() = default;

void PatchDBViewer::createElements()
{
    setSize(750, 450); // cleaner obvs
    tableModel = std::make_unique<PatchDBSQLTableModel>(editor, storage);
    table = std::make_unique<juce::TableListBox>("Patch Table", tableModel.get());
    table->getHeader().addColumn("id", 1, 40);
    table->getHeader().addColumn("name", 2, 200);
    table->getHeader().addColumn("category", 3, 250);
    table->getHeader().addColumn("author", 4, 200);

    table->setBounds(0, 50, getWidth(), getHeight() - 50);
    addAndMakeVisible(*table);

    nameTypein = std::make_unique<juce::TextEditor>("Patch Name");
    nameTypein->setBounds(10, 10, 400, 30);
    nameTypein->addListener(this);
    addAndMakeVisible(*nameTypein);

    executeQuery();
}

void PatchDBViewer::executeQuery()
{
    tableModel->executeQuery(nameTypein->getText().toStdString());
    table->updateContent();
}
void PatchDBViewer::textEditorTextChanged(juce::TextEditor &editor) { executeQuery(); }
