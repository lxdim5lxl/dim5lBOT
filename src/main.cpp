#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
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

static PlayLayer* getPlayLayer() {
    return PlayLayer::get();
}

std::filesystem::path getSavePath() {
    auto path = std::filesystem::path(geode::dirs::getSaveDir()) / "dim5lBOT";
    std::filesystem::create_directories(path);
    return path / "macro.txt";
}

void saveMacro() {
    std::ofstream file(getSavePath());
    int pressFrame = -1;
    for (size_t i = 0; i < g.inputs.size(); i++) {
        auto& inp = g.inputs[i];
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

void loadMacro() {
    std::ifstream file(getSavePath());
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

// ===== PlayerObject Hook - 입력 감지 =====
class $modify(MyPlayerObject, PlayerObject) {
    void pushButton(PlayerButton btn) {
        PlayerObject::pushButton(btn);
        if (!getPlayLayer()) return;
        if (g.recording && btn == PlayerButton::Jump) {
            bool isP2 = getPlayLayer()->m_player2 == this;
            g.inputs.push_back({g.frame, true, isP2});
        }
    }

    void releaseButton(PlayerButton btn) {
        PlayerObject::releaseButton(btn);
        if (!getPlayLayer()) return;
        if (g.recording && btn == PlayerButton::Jump) {
            bool isP2 = getPlayLayer()->m_player2 == this;
            g.inputs.push_back({g.frame, false, isP2});
        }
    }
};

// ===== PlayLayer Hook =====
class $modify(MyPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        g.frame = 0;
        g.playIndex = 0;
        if (g.recording) g.inputs.clear();
    }

    void update(float dt) {
        if (g.playing) {
            while (g.playIndex < (int)g.inputs.size() &&
                   g.inputs[g.playIndex].frame == g.frame) {
                auto& input = g.inputs[g.playIndex];
                auto player = input.player2 ? m_player2 : m_player1;
                if (player) {
                    if (input.pressed)
                        player->pushButton(PlayerButton::Jump);
                    else
                        player->releaseButton(PlayerButton::Jump);
                }
                g.playIndex++;
            }
        }

        PlayLayer::update(dt);

        if (g.recording || g.playing) g.frame++;
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
        else if (!g.inputs.empty()) {
            auto str = fmt::format("Loaded ({} inputs)", g.inputs.size());
            statusLabel->setString(str.c_str());
        } else {
            statusLabel->setString("Idle - No macro");
        }
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
        g.recording = false;
        g.playing = false;
        updateStatus();
        FLAlertLayer::create("dim5lBOT", "Stopped!", "OK")->show();
    }

    void onSave(CCObject*) {
        if (g.inputs.empty()) {
            FLAlertLayer::create("dim5lBOT", "Nothing to save!", "OK")->show();
            return;
        }
        saveMacro();
        FLAlertLayer::create("dim5lBOT", "Macro saved!", "OK")->show();
    }

    void onLoad(CCObject*) {
        loadMacro();
        if (g.inputs.empty()) {
            FLAlertLayer::create("dim5lBOT", "No save file found!", "OK")->show();
            return;
        }
        auto str = fmt::format("Loaded {} inputs!", g.inputs.size());
        FLAlertLayer::create("dim5lBOT", str.c_str(), "OK")->show();
        updateStatus();
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
