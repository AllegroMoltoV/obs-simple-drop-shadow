# OBS Forum の AI 利用開示要件

確認日: 2026-09-22

## 結論

[Forum Resource and IP Policy](https://obsproject.com/forum/threads/forum-resource-and-ip-policy.178569/) は、AI または LLM をコード、画像、その他の制作に使用したリソースに対し、使用したツールと用途をリソース説明へ記載するよう求めている。同ポリシーの `disclaimer` は、損害賠償責任を制限する免責条項ではなく、AI 利用の開示文を指す。

公式の定型文は掲載されていない。リソース編集画面にも AI 専用欄はなく、通常の `Description` 欄へ開示文を追加する。公開中の承認例では、見出しと文章が統一されていない。この差異により、固定文言より、実際の利用状況を具体的に説明する点が審査対象と判断できる。

AI を全体または大部分の作成に使用したリソースは許可対象外とされる。開示文の追加だけでは承認を保証しない。最終判断は OBS Forum のモデレーションチームに属する。

## 適用時期

AI 利用開示の義務は 2026-01-20 にポリシーへ追加され、2026-07-12 に説明と参照先が補強された。Simple Drop Shadow の初回公開日は 2026-01-05 であり、当初の公開は義務化より前だった。今回の更新は義務化後に提出されている。

[Resource Security Incident](https://obsproject.com/forum/threads/resource-security-incident.194555/) によると、2026-03-09 以降は既存リソースの更新も手動審査の対象である。現在の更新審査も、この時系列と一致する。

## 開示文に必要な内容

最低限、次の事実を英語で記載する。

- 使用した AI ツール名と提供元
- AI を使用した対象: ソースコード、テスト、ビルド設定、文書、リソース説明、画像など
- 使用方法と関与の程度: 補助、個別修正、広範な実装支援など
- 人間が担当した要件決定、レビュー、動作確認の範囲

ポリシーには、利用率、プロンプト履歴、モデルの版、コード行数の記載要件がない。確認できない人間レビューを記載せず、実施済みの試験と手動確認を具体的に示す方が正確である。日本語と英語の説明を併記する現在の構成では、開示文も両言語で併記すると読者へ同じ情報を提供できる。

## Simple Drop Shadow への適用

2026-09-19 以降の作業記録では、OpenAI Codex が次の作業を支援した。

- 分離型ボックスブラーの横方向・縦方向パスと合成処理の実装
- Direct3D 11 の GPU 統合テスト、画像端、正負オフセット、半径 0・1・20、色、不透明度の検査
- ビルド、配布物照合、GitHub Release、OBS Forum 更新の手順作成
- README、調査報告、リリースノート、Forum 更新文、issue 返信文案の作成と改稿

利用者は Windows の OBS 31.1.1 で、フィルタ表示、半径 0・4・20、X・Y 位置、色、不透明度を手動確認した。Direct3D 11 の統合テスト 11 件も成功している。詳細は [分離型ボックスブラーの実装と検証](2026-09-19-issue-1-implementation.md)にある。

現存記録だけでは、作業開始前の v0.1.0 における AI 利用、利用者自身によるコードレビューの範囲、リソースアイコンと GIF の制作方法を確定できない。最終開示文には、この 3 点の事実確認が必要である。

## 文章構造

次の英文は公式テンプレートではなく、必要事項を欠落させないための構造例である。角括弧内は確認済みの事実へ置換する。

```text
AI disclosure

OpenAI Codex [and other tools, if applicable] was used [extent] during development. It assisted with [specific code areas], automated tests, build and release tooling, and documentation drafts, including revisions to this resource description. The maintainer defined the requirements, directed the work, [state the exact review scope], and validated the released build through [specific automated and manual tests]. [State any AI use in images or other assets.]
```

## 公開例

- [SceneAnchor](https://obsproject.com/forum/resources/sceneanchor.2666/) は Claude Code の利用、説明文への利用、作者によるレビュー・ビルド・Windows 実機確認を記載している。
- [Lumetric Corrector 3.0.0](https://obsproject.com/forum/resources/lumetric-corrector.2137/updates) は、Anthropic Claude と OpenAI Codex、対象となったコード・シェーダー・テスト・文書、保守者による試験を列挙している。
- [Halsu plugins for streaming](https://obsproject.com/forum/resources/halsu-plugins-for-streaming.2374/) は、人間が作成した中核部分と AI を使用した追加機能・定型コード・ビルド基盤を区別している。

これらは公開済みリソースの実例であり、OBS Forum が配布する公式テンプレートではない。

## 関連する公式方針

[OBS Studio Contribution Guidelines](https://github.com/obsproject/obs-studio/blob/master/CONTRIBUTING.md#aimachine-learning-policy) は、OBS 本体への提出物に対して、コード、説明、issue、コメントを人間が記述する方針を示している。Forum ポリシーはこの文書を背景説明として参照する一方、Forum リソースには AI 利用の開示を要求している。両文書の対象を混同せず、Forum 掲載では Forum Resource and IP Policy を直接の要件として扱う。
