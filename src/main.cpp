#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <vector>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;

struct MacroInput {
    int frame;
    bool pressed;
    bool player2;
};

struct GlobalState {
    std::vector<MacroInput> inputs;
    bool recording = false;
    bool playing = false;
    int frame = 0;
    int playIndex = 0;
};

static GlobalState g;

std::filesystem::path getMacroDir() {
    auto path = std::filesystem::path(geode::dirs::getSaveDir()) / "dim5lBOT";
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path getTempPath() {
    return getMacroDir() / "temp.txt";
}

void writeMacroToFile(std::filesystem::path path) {
    std::ofstream file(path);
    int pressFrame = -1;
    for (auto& inp : g.inputs) {
        if (!inp.player2) {
            if (inp.pressed) {
                pressFrame = inp.frame;
            } else if (pressFrame >= 0) {
                file << pressFrame << "," << inp.frame << "\n";
                pressFrame = -1;
            }
        }
    }
    file.close();
}

void readMacroFromFile(std::filesystem::path path) {
    std::ifstream file(path);
    if (!file.is_open()) return;
    g.inputs.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto comma = line.find(',');
        if (comma == std::string::npos) continue;
        int pressF = std::stoi(line.substr(0, comma));
        int releaseF = std::stoi(line.substr(comma + 1));
        g.inputs.push_back({pressF, true, false});
        g.inputs.push_back({releaseF, false, false});
    }
    std::sort(g.inputs.begin(), g.inputs.end(), [](const MacroInput& a, const MacroInput& b) {
        return a.frame < b.frame;
    });
    file.close();
}

// ===== GJBaseGameLayer Hook - xdBot 방식 =====
class $modify(MyGJBaseGameLayer, GJBaseGameLayer) {
    struct Fields {
        bool macroInput = false;
    };

    void handleButton(bool hold, int button, bool player2) {
        if (g.playing) {
            if (!m_fields->macroInput) {
                GJBaseGameLayer::handleButton(hold, button, player2);
                return;
            }
        }

        GJBaseGameLayer::handleButton(hold, button, player2);

        if (!g.recording) return;
        if (!m_player1 || m_player1->m_isDead) return;

        g.inputs.push_back({g.frame, hold, player2});
    }
};

// ===== PlayLayer Hook =====
class $modify(MyPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        g.frame = 0;
        g.playIndex = 0;
    }

    void update(float dt) {
        if (g.playing) {
            auto& fields = static_cast<MyGJBaseGameLayer*>(static_cast<GJBaseGameLayer*>(this))->m_fields.self();

            while (g.playIndex < (int)g.inputs.size() &&
                   g.inputs[g.playIndex].frame == g.frame) {
                auto& input = g.inputs[g.playIndex];
                fields->macroInput = true;
                handleButton(input.pressed, 1, input.player2);
                fields->macroInput = false;
                g.playIndex++;
            }
        }

        PlayLayer::update(dt);

        if (g.recording || g.playing) g.frame++;
    }
};

// ===== Save Name Popup =====
class SaveNamePopup : public Popup<> {
public:
    TextInput* nameInput = nullptr;

    static SaveNamePopup* create() {
        auto ret = new SaveNamePopup();
        if (ret->initAnchored(300, 160)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool setup() override {
        setTitle("Save Macro");

        nameInput = TextInput::create(220, "Enter filename...");
        nameInput->setPosition({150, 90});
        m_mainLayer->addChild(nameInput);

        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(SaveNamePopup::onSave)
        );
        auto cancelBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Cancel", "goldFont.fnt", "GJ_button_06.png"),
            this, menu_selector(SaveNamePopup::onCancel)
        );

        auto menu = CCMenu::create(saveBtn, cancelBtn, nullptr);
        menu->alignItemsHorizontallyWithPadding(10);
        menu->setPosition({150, 40});
        m_mainLayer->addChild(menu);

        return true;
    }

    void onSave(CCObject*) {
        auto name = nameInput->getString();
        if (name.empty()) {
            FLAlertLayer::create("dim5lBOT", "Please enter a filename!", "OK")->show();
            return;
        }
        auto path = getMacroDir() / (name + ".txt");
        std::filesystem::copy_file(getTempPath(), path,
            std::filesystem::copy_options::overwrite_existing);
        FLAlertLayer::create("dim5lBOT", ("Saved as: " + name + ".txt").c_str(), "OK")->show();
        onClose(nullptr);
    }

    void onCancel(CCObject*) { onClose(nullptr); }
};

// ===== Load List Popup =====
class LoadListPopup : public Popup<> {
public:
    static LoadListPopup* create() {
        auto ret = new LoadListPopup();
        if (ret->initAnchored(320, 280)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool setup() override {
        setTitle("Load Macro");

        auto dir = getMacroDir();
        int y = 210;
        bool any = false;

        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            auto path = entry.path();
            if (path.extension() != ".txt") continue;
            if (path.filename() == "temp.txt") continue;

            any = true;
            auto name = path.stem().string();

            auto btn = CCMenuItemSpriteExtra::create(
                ButtonSprite::create(name.c_str(), "chatFont.fnt", "GJ_button_04.png"),
                this, menu_selector(LoadListPopup::onLoadFile)
            );
            btn->setUserObject(CCString::create(path.string()));
            btn->setScale(0.8f);

            auto menu = CCMenu::create(btn, nullptr);
            menu->setPosition({160, (float)y});
            m_mainLayer->addChild(menu);
            y -= 45;
        }

        if (!any) {
            auto label = CCLabelBMFont::create("No macros found!", "chatFont.fnt");
            label->setPosition({160, 130});
            label->setScale(0.7f);
            m_mainLayer->addChild(label);
        }

        return true;
    }

    void onLoadFile(CCObject* sender) {
        auto item = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto pathStr = static_cast<CCString*>(item->getUserObject())->getCString();
        readMacroFromFile(pathStr);
        FLAlertLayer::create("dim5lBOT", fmt::format("Loaded {} inputs!", g.inputs.size()).c_str(), "OK")->show();
        onClose(nullptr);
    }
};

// ===== Bot Menu =====
class BotMenuLayer : public CCLayer {
public:
    CCLabelBMFont* statusLabel = nullptr;

    static BotMenuLayer* create() {
        auto ret = new BotMenuLayer();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() {
        if (!CCLayer::init()) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto dimBg = CCLayerColor::create({0, 0, 0, 120});
        dimBg->setContentSize(winSize);
        addChild(dimBg);

        auto panel = CCScale9Sprite::create("GJ_square01.png");
        panel->setContentSize({300, 340});
        panel->setPosition(winSize / 2);
        addChild(panel);

        auto title = CCLabelBMFont::create("dim5lBOT", "goldFont.fnt");
        title->setPosition(winSize.width / 2, winSize.height / 2 + 140);
        title->setScale(0.9f);
        addChild(title);

        statusLabel = CCLabelBMFont::create("Idle", "chatFont.fnt");
        statusLabel->setPosition(winSize.width / 2, winSize.height / 2 + 112);
        statusLabel->setScale(0.6f);
        statusLabel->setColor({255, 255, 100});
        addChild(statusLabel);
        updateStatus();

        auto recBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Record", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(BotMenuLayer::onRecord)
        );
        auto playBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Play", "goldFont.fnt", "GJ_button_02.png"),
            this, menu_selector(BotMenuLayer::onPlay)
        );
        auto stopBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Stop", "goldFont.fnt", "GJ_button_06.png"),
            this, menu_selector(BotMenuLayer::onStop)
        );
        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_03.png"),
            this, menu_selector(BotMenuLayer::onSave)
        );
        auto loadBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Load", "goldFont.fnt", "GJ_button_04.png"),
            this, menu_selector(BotMenuLayer::onLoad)
        );
        auto closeBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Close", "goldFont.fnt", "GJ_button_05.png"),
            this, menu_selector(BotMenuLayer::onClose)
        );

        auto menu = CCMenu::create(recBtn, playBtn, stopBtn, saveBtn, loadBtn, closeBtn, nullptr);
        menu->alignItemsVerticallyWithPadding(5);
        menu->setPosition(winSize / 2);
        addChild(menu);

        return true;
    }

    void updateStatus() {
        if (!statusLabel) return;
        if (g.recording)
            statusLabel->setString("Recording...");
        else if (g.playing)
            statusLabel->setString("Playing...");
        else if (!g.inputs.empty())
            statusLabel->setString(fmt::format("Loaded ({} inputs)", g.inputs.size()).c_str());
        else
            statusLabel->setString("Idle - No macro");
    }

    void onRecord(CCObject*) {
        g.inputs.clear();
        g.recording = true;
        g.playing = false;
        g.frame = 0;
        removeFromParentAndCleanup(true);
    }

    void onPlay(CCObject*) {
        if (g.inputs.empty()) {
            FLAlertLayer::create("dim5lBOT", "No macro loaded!", "OK")->show();
            return;
        }
        g.recording = false;
        g.playing = true;
        g.playIndex = 0;
        g.frame = 0;
        removeFromParentAndCleanup(true);
    }

    void onStop(CCObject*) {
        bool wasRecording = g.recording;
        g.recording = false;
        g.playing = false;

        if (wasRecording && !g.inputs.empty()) {
            writeMacroToFile(getTempPath());
            FLAlertLayer::create("dim5lBOT", fmt::format("Stopped! {} inputs recorded.\nUse Save to name it.", g.inputs.size()).c_str(), "OK")->show();
        } else {
            FLAlertLayer::create("dim5lBOT", "Stopped!", "OK")->show();
        }
        updateStatus();
    }

    void onSave(CCObject*) {
        if (!std::filesystem::exists(getTempPath())) {
            FLAlertLayer::create("dim5lBOT", "No temp file! Record first.", "OK")->show();
            return;
        }
        auto popup = SaveNamePopup::create();
        if (auto parent = getParent())
            parent->addChild(popup, 200);
    }

    void onLoad(CCObject*) {
        auto popup = LoadListPopup::create();
        if (auto parent = getParent())
            parent->addChild(popup, 200);
    }

    void onClose(CCObject*) {
        removeFromParentAndCleanup(true);
    }
};

void openBotMenu(CCNode* parent) {
    auto layer = BotMenuLayer::create();
    parent->addChild(layer, 100);
}

class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto botBtn = CCMenuItemSpriteExtra::create(
            CircleButtonSprite::createWithSpriteFrameName("geode.loader/geode-logo-outline-gold.png"),
            this, menu_selector(MyMenuLayer::onBotMenu)
        );
        auto menu = CCMenu::create(botBtn, nullptr);
        menu->setPosition(CCPoint(30, 30));
        addChild(menu, 10);
        return true;
    }
    void onBotMenu(CCObject*) { openBotMenu(this); }
};

class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto botBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("BOT", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(MyPauseLayer::onBotMenu)
        );
        botBtn->setScale(0.8f);
        auto menu = CCMenu::create(botBtn, nullptr);
        menu->setPosition(CCPoint(70, 200));
        addChild(menu, 10);
    }
    void onBotMenu(CCObject*) { openBotMenu(this); }
};

class $modify(MyEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        auto botBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("BOT", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(MyEndLevelLayer::onBotMenu)
        );
        botBtn->setScale(0.8f);
        auto menu = CCMenu::create(botBtn, nullptr);
        menu->setPosition(CCPoint(70, 200));
        addChild(menu, 10);
    }
    void onBotMenu(CCObject*) { openBotMenu(this); }
};
