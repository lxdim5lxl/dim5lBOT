#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
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
    bool isPlayingMacro = false; // 재생중 입력을 녹화 안하기 위한 플래그
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
    for (auto& inp : g.inputs) {
        file << inp.frame << "," << (inp.pressed ? 1 : 0) << "," << (inp.player2 ? 1 : 0) << "\n";
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
        std::istringstream ss(line);
        std::string token;
        std::vector<std::string> parts;
        while (std::getline(ss, token, ',')) parts.push_back(token);
        if (parts.size() < 3) continue;
        MacroInput inp;
        inp.frame = std::stoi(parts[0]);
        inp.pressed = parts[1] == "1";
        inp.player2 = parts[2] == "1";
        g.inputs.push_back(inp);
    }
    file.close();
}

// ===== GJBaseGameLayer Hook - 녹화 =====
class $modify(MyGJBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool hold, int button, bool player2) {
        GJBaseGameLayer::handleButton(hold, button, player2);

        // 재생 중 입력은 녹화하지 않음
        if (g.isPlayingMacro) return;
        if (!g.recording) return;
        if (!m_player1 || m_player1->m_isDead) return;

        g.inputs.push_back({g.frame, hold, player2});
    }
};

// ===== PlayLayer Hook - 프레임 카운트 및 재생 =====
class $modify(MyPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        // 리스폰 시 프레임 및 재생 인덱스 초기화
        g.frame = 0;
        g.playIndex = 0;
    }

    void update(float dt) {
        // update 호출 전에 재생 입력 주입
        // 이렇게 해야 해당 프레임의 물리 연산에 반영됨
        if (g.playing) {
            while (g.playIndex < (int)g.inputs.size() &&
                   g.inputs[g.playIndex].frame == g.frame) {
                auto& input = g.inputs[g.playIndex];
                auto player = input.player2 ? m_player2 : m_player1;
                if (player) {
                    g.isPlayingMacro = true;
                    if (input.pressed)
                        player->pushButton(PlayerButton::Jump);
                    else
                        player->releaseButton(PlayerButton::Jump);
                    g.isPlayingMacro = false;
                }
                g.playIndex++;
            }
        }

        PlayLayer::update(dt);

        // update 후 프레임 증가
        if (g.recording || g.playing) g.frame++;
    }
};

// ===== Save Name Layer =====
class SaveNameLayer : public CCLayer {
public:
    TextInput* nameInput = nullptr;

    static SaveNameLayer* create() {
        auto ret = new SaveNameLayer();
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

        auto dimBg = CCLayerColor::create({0, 0, 0, 150});
        dimBg->setContentSize(winSize);
        addChild(dimBg);

        auto panel = CCScale9Sprite::create("GJ_square01.png");
        panel->setContentSize({280, 160});
        panel->setPosition(winSize / 2);
        addChild(panel);

        auto title = CCLabelBMFont::create("Save Macro", "goldFont.fnt");
        title->setPosition(winSize.width / 2, winSize.height / 2 + 60);
        title->setScale(0.7f);
        addChild(title);

        nameInput = TextInput::create(200, "Enter filename...");
        nameInput->setPosition(winSize / 2);
        addChild(nameInput);

        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_01.png"),
            this, menu_selector(SaveNameLayer::onSave)
        );
        auto cancelBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Cancel", "goldFont.fnt", "GJ_button_06.png"),
            this, menu_selector(SaveNameLayer::onCancel)
        );

        auto menu = CCMenu::create(saveBtn, cancelBtn, nullptr);
        menu->alignItemsHorizontallyWithPadding(10);
        menu->setPosition(winSize.width / 2, winSize.height / 2 - 50);
        addChild(menu);

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
        removeFromParentAndCleanup(true);
    }

    void onCancel(CCObject*) { removeFromParentAndCleanup(true); }
};

// ===== Load List Layer =====
class LoadListLayer : public CCLayer {
public:
    static LoadListLayer* create() {
        auto ret = new LoadListLayer();
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

        auto dimBg = CCLayerColor::create({0, 0, 0, 150});
        dimBg->setContentSize(winSize);
        addChild(dimBg);

        auto panel = CCScale9Sprite::create("GJ_square01.png");
        panel->setContentSize({300, 280});
        panel->setPosition(winSize / 2);
        addChild(panel);

        auto title = CCLabelBMFont::create("Load Macro", "goldFont.fnt");
        title->setPosition(winSize.width / 2, winSize.height / 2 + 120);
        title->setScale(0.7f);
        addChild(title);

        auto dir = getMacroDir();
        float y = winSize.height / 2 + 70;
        bool any = false;

        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            auto path = entry.path();
            if (path.extension() != ".txt") continue;
            if (path.filename() == "temp.txt") continue;

            any = true;
            auto name = path.stem().string();

            auto btn = CCMenuItemSpriteExtra::create(
                ButtonSprite::create(name.c_str(), "chatFont.fnt", "GJ_button_04.png"),
                this, menu_selector(LoadListLayer::onLoadFile)
            );
            btn->setUserObject(CCString::create(path.string()));
            btn->setScale(0.8f);

            auto menu = CCMenu::create(btn, nullptr);
            menu->setPosition(winSize.width / 2, y);
            addChild(menu);
            y -= 45;
        }

        if (!any) {
            auto label = CCLabelBMFont::create("No macros found!", "chatFont.fnt");
            label->setPosition(winSize / 2);
            label->setScale(0.7f);
            addChild(label);
        }

        auto closeBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Close", "goldFont.fnt", "GJ_button_05.png"),
            this, menu_selector(LoadListLayer::onClose)
        );
        auto closeMenu = CCMenu::create(closeBtn, nullptr);
        closeMenu->setPosition(winSize.width / 2, winSize.height / 2 - 110);
        addChild(closeMenu);

        return true;
    }

    void onLoadFile(CCObject* sender) {
        auto item = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto pathStr = static_cast<CCString*>(item->getUserObject())->getCString();
        readMacroFromFile(pathStr);
        FLAlertLayer::create("dim5lBOT",
            fmt::format("Loaded {} inputs!", g.inputs.size()).c_str(), "OK")->show();
        removeFromParentAndCleanup(true);
    }

    void onClose(CCObject*) { removeFromParentAndCleanup(true); }
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
        g.playIndex = 0;
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
        // 레벨을 처음부터 재시작해야 frame이 맞음
        if (auto pl = PlayLayer::get()) {
            pl->resetLevelFromStart();
        }
        removeFromParentAndCleanup(true);
    }

    void onStop(CCObject*) {
        bool wasRecording = g.recording;
        g.recording = false;
        g.playing = false;

        if (wasRecording && !g.inputs.empty()) {
            writeMacroToFile(getTempPath());
            FLAlertLayer::create("dim5lBOT",
                fmt::format("Stopped! {} inputs.\nUse Save to name it.", g.inputs.size()).c_str(),
                "OK")->show();
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
        if (auto parent = getParent()) {
            auto layer = SaveNameLayer::create();
            parent->addChild(layer, 200);
        }
    }

    void onLoad(CCObject*) {
        if (auto parent = getParent()) {
            auto layer = LoadListLayer::create();
            parent->addChild(layer, 200);
        }
    }

    void onClose(CCObject*) { removeFromParentAndCleanup(true); }
};

void openBotMenu(CCNode* parent) {
    auto layer = BotMenuLayer::create();
    parent->addChild(layer, 100);
}

// 메인 메뉴 버튼 제거 - PauseLayer와 EndLevelLayer만 유지

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
