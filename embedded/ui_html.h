#pragma once

// 嵌入的HTML内容 - 多个原始字符串字面量拼接
const char* g_html_content = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WinUI3 Style Tabs</title>
    <script>
        window.onerror = function(msg, url, line, col, error) {
            return true;
        };
    </script>
    <style>
        
        :root {
            
            --color-primary: #0078d4;
            --color-primary-hover: #106ebe;
            --color-primary-pressed: #005a9e;
            --color-text-primary: #1d1d1d;
            --color-text-secondary: #494949;
            --color-text-tertiary: #6e6e6e;
            --color-background: #ffffff;
            --color-background-secondary: #f2f2f2;
            --color-background-hover: #f7f7f7;
            --color-border: #e0e0e0;
            --color-border-hover: #c8c8c8;
            --color-border-focus: #0078d4;
            
            
            --radius-sm: 3px;
            --radius-md: 6px;
            --radius-lg: 8px;
   )HTML" \
R"HTML(         --radius-full: 9999px;
            
            
            --shadow-sm: 0 1px 3px rgba(0, 0, 0, 0.06);
            --shadow-md: 0 2px 8px rgba(0, 0, 0, 0.1);
            --shadow-lg: 0 4px 16px rgba(0, 0, 0, 0.12);
            --shadow-xl: 0 8px 24px rgba(0, 0, 0, 0.15);
            --shadow-elevated: 0 4px 12px rgba(0, 0, 0, 0.08), 0 8px 24px rgba(0, 0, 0, 0.08);
            
            
            --spacing-xs: 3px;
            --spacing-sm: 6px;
            --spacing-md: 10px;
            --spacing-lg: 14px;
            --spacing-xl: 18px;
            
            
            --easing-standard: cubic-bezier(0.4, 0, 0.2, 1);      
            --easing-enter: cubic-bezier(0, 0, 0.2, 1);           
            --easing-exit: cubic-bezier(0.4, 0, 1, 1);            
            --easing-bounce: cubic-bezier(0.34, 1.56, 0.64, 1);   
            --easing-smooth: cubic-bezier(0.25, 0.1, 0.25, 1);    
            --easing-spring: cubic-bezier(0.175, 0.885, 0.32, 1.275); 
      )HTML" \
R"HTML(      
            
            --duration-fast: 35ms;
            --duration-normal: 65ms;
            --duration-slow: 90ms;
            --duration-slower: 130ms;
        }
        
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Helvetica Neue', Arial, sans-serif;
            background:
                radial-gradient(circle at 14% 18%, rgba(0, 120, 212, 0.08), transparent 24%),
                radial-gradient(circle at 86% 12%, rgba(16, 110, 190, 0.06), transparent 22%),
                radial-gradient(circle at 82% 84%, rgba(0, 120, 212, 0.05), transparent 26%),
                linear-gradient(180deg, #fbfdff 0%, #f6f9fd 100%);
            height: 100vh;
            overflow: visible;
            color: var(--color-text-primary);
            font-size: 11px;
            font-weight: 400;
            position: relative;
        }

        body::before {
            content: '';
   )HTML" \
R"HTML(         position: fixed;
            inset: -30px;
            pointer-events: none;
            z-index: 0;
            background:
                radial-gradient(circle at 20% 20%, rgba(0, 120, 212, 0.10), transparent 26%),
                radial-gradient(circle at 80% 16%, rgba(0, 120, 212, 0.07), transparent 22%),
                radial-gradient(circle at 82% 78%, rgba(16, 110, 190, 0.06), transparent 24%),
                radial-gradient(circle at 14% 84%, rgba(0, 120, 212, 0.05), transparent 20%);
            filter: blur(18px);
            opacity: 0.72;
            animation: ambientDrift 18s ease-in-out infinite alternate;
        }
        
        
        .title-bar {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            height: 32px;
            background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%);
            display: flex;
            align-items: center;
            justify-content: space-between;
            )HTML" \
R"HTML(padding: 0 8px;
            z-index: 99999;
            cursor: default;
            user-select: none;
            border-bottom: 1px solid rgba(0, 0, 0, 0.08);
            box-shadow: 0 1px 3px rgba(0, 0, 0, 0.05);
        }
        
        .title-bar-left {
            display: flex;
            align-items: center;
            gap: 8px;
        }
        
        .title-bar-icon {
            width: 18px;
            height: 18px;
            background: linear-gradient(135deg, #0078d4, #106ebe);
            border-radius: 4px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 10px;
            color: white;
            font-weight: 400;
            box-shadow: 0 1px 2px rgba(0, 120, 212, 0.3);
        }
        
        .title-bar-title {
            color: #1d1d1d;
            font-size: 12px;
            font-weight: 400;
            letter-spacing: 0.5px;
            white-space: nowrap;
        }
        
 )HTML" \
R"HTML(       .title-bar-controls {
            display: flex;
        }
        
        .title-bar-button {
            width: 46px;
            height: 32px;
            border: none;
            background: transparent;
            color: #494949;
            font-size: 12px;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: background 0.15s ease;
        }
        
        .title-bar-button:hover {
            background: rgba(0, 0, 0, 0.05);
        }
        
        .title-bar-button.close:hover {
            background: #e81123;
            color: white;
        }
        
        .title-bar-button svg {
            width: 12px;
            height: 12px;
            fill: currentColor;
        }
        
        
        
        .helper-info {
            
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            height: 20px;
         )HTML" \
R"HTML(   display: flex;
            align-items: center;
            padding: 4px 8px;
            background: var(--color-background-secondary);
            border-top: 1px solid var(--color-border);
            font-size: 9px;
            color: var(--color-text-secondary);
            width: 100%;
            
            z-index: 1;
            
            margin: 0;
            padding: 0 8px 0 8.5px; 
        }
        
        
        .helper-label {
            font-weight: 400;
            color: var(--color-primary);
            margin-right: 8px;
            white-space: nowrap;
        }
        
        
        .helper-text {
            flex: 1;
            transition: all 0.2s ease;
            min-width: 0;
        }
        
        
        .helper-text.slide-in {
            animation: slideIn 0.2s ease-out;
        }

        
        #speed-slider {
            -webkit-appearance: none;
            height: 4px;
            background: var(--color-background-secondary))HTML" \
R"HTML(;
            border-radius: 2px;
            outline: none;
            transition: background 0.2s ease;
        }

        #speed-slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background: var(--color-primary);
            cursor: pointer;
            transition: all 0.2s cubic-bezier(0.175, 0.885, 0.32, 1.275);
            box-shadow: 0 0 2px rgba(0, 0, 0, 0.2);
        }

        #speed-slider::-webkit-slider-thumb:hover {
            transform: scale(1.2);
            box-shadow: 0 0 0 4px rgba(0, 120, 212, 0.1);
        }

        #speed-slider::-webkit-slider-thumb:active {
            transform: scale(1.1);
            box-shadow: 0 0 0 6px rgba(0, 120, 212, 0.2);
        }

        @keyframes slideIn {
            from { 
                opacity: 0;
                transform: translateX(-10px); 
            }
            to { 
    )HTML" \
R"HTML(            opacity: 1;
                transform: translateX(0); 
            }
        }
        
        
        .container {
            width: 100%;
            
            height: 197px;
            
            top: 598px;
            display: flex;
            flex-direction: column;
            background: var(--color-background);
            box-shadow: var(--shadow-sm);
            position: absolute;
            left: 0;
            right: 0;
            padding-top: 0;
            
            z-index: 10;
        }
        
        
        .tabs {
            display: flex;
            background: var(--color-background);
            border-bottom: 1px solid var(--color-border);
            padding: 0;
            gap: 0;
            overflow-x: hidden;
            overflow-y: hidden;
            justify-content: flex-start;
        }
        
        
        .content {
            
            height: 152px;
            overflow-y: auto;
            padding: var(--sp)HTML" \
R"HTML(acing-md);
            background: var(--color-background);
        }
        .tabs::-webkit-scrollbar { height: 6px; }
        .tabs::-webkit-scrollbar-track { background: var(--color-background); }
        .tabs::-webkit-scrollbar-thumb { background: var(--color-border-hover); border-radius: var(--radius-full); transition: all var(--duration-normal) var(--easing-standard); }
        .tabs::-webkit-scrollbar-thumb:hover { background: var(--color-primary); box-shadow: var(--shadow-sm); }
        
        
        .tab-actions {
            display: flex;
            gap: 8px;
            margin-left: auto;
            align-items: center;
            flex-wrap: nowrap;
            flex: 0 0 auto;
            padding: 0 8px;
        }
        
        
        .tab-action-button {
            height: 24px;
            border: none;
            border-radius: var(--radius-sm);
            background: transparent;
            cursor: pointer;
            display: flex;
            align-i)HTML" \
R"HTML(tems: center;
            gap: 4px;
            padding: 0 8px;
            transition: background-color 0.1s linear;
            position: relative;
            flex: 0 0 auto;
            min-width: fit-content;
            white-space: nowrap;
            justify-content: center;
            color: var(--color-text-primary);
        }
        
        .tab-action-button:hover {
            background: var(--color-background-hover);
        }
        
        .tab-action-button:active {
            background: var(--color-background-secondary);
        }

        .tab-action-button:disabled {
            cursor: default;
            opacity: 0.7;
        }

        .tab-action-label {
            display: inline-flex;
            align-items: center;
            font-size: 9px;
            font-weight: 500;
            line-height: 1;
            color: inherit;
        }
        
        
        .icon-copy,
        .icon-key,
        .icon-refresh,
        .icon-refresh-nologin,
  )HTML" \
R"HTML(      .icon-mute,
        .icon-clear-cache {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            flex: 0 0 13px;
            width: 13px;
            height: 13px;
            position: relative;
            color: inherit;
        }

        .icon-copy svg,
        .icon-key svg,
        .icon-refresh svg,
        .icon-refresh-nologin svg,
        .icon-mute svg,
        .icon-clear-cache svg {
            display: block;
            width: 13px;
            height: 13px;
            overflow: visible;
        }

        .icon-copy svg *,
        .icon-key svg *,
        .icon-refresh svg *,
        .icon-refresh-nologin svg *,
        .icon-mute svg *,
        .icon-clear-cache svg * {
            fill: none;
            stroke: currentColor;
            stroke-width: 1.5;
            stroke-linecap: round;
            stroke-linejoin: round;
            vector-effect: non-scaling-stroke;
        }
        
        
  )HTML" \
R"HTML(      .tab-action-button:hover .icon-refresh svg {
            animation: rotate 1s linear infinite;
        }

        
        .icon-mute .mute-slash { display: none; }
        .tab-action-button.muted .icon-mute .mute-wave { display: none; }
        .tab-action-button.muted .icon-mute .mute-slash { display: inline; }
        
        
        .tab-action-button.muted,
        .tab-action-button.cache-clearing,
        .tab-action-button.cache-cleared {
            color: var(--color-primary);
        }

        .tab-action-button.cache-clear-failed {
            color: #c62828;
        }
        
        
        @keyframes rotate {
            from { transform: rotate(0deg); }
            to { transform: rotate(360deg); }
        }
        
        
        .tabs {
            overflow-x: hidden;
        }
        
        
        .tab-action-button {
            width: auto;
            min-width: 22px;
            height: 22px;
            gap: 2px;
            padding: 0 4px;
 )HTML" \
R"HTML(       }
        
        
        .icon-copy svg,
        .icon-key svg,
        .icon-refresh svg,
        .icon-refresh-nologin svg,
        .icon-mute svg,
        .icon-clear-cache svg {
            width: 13px;
            height: 13px;
        }
        
        
        .tab-actions {
            padding: 0 1px;
            gap: 0;
        }
        .tab-button { flex: 0 0 auto; padding: var(--spacing-sm) 12px; border: none; border-radius: 0; background: transparent; color: var(--color-text-secondary); font-size: 10.5px; font-weight: 400; cursor: pointer; transition: all var(--duration-normal) var(--easing-standard); position: relative; outline: none; opacity: 0; transform: translateY(-4px); animation: slideUp var(--duration-normal) var(--easing-enter) forwards; margin-bottom: -1px; min-width: 78px; text-align: center; } 
        .tab-button:nth-child(1) { animation-delay: 0.1s; }
        .tab-button:nth-child(2) { animation-delay: 0.15s; }
        .tab-button:nth-child(3) { an)HTML" \
R"HTML(imation-delay: 0.2s; }
        .tab-button:hover { color: var(--color-primary); background: var(--color-background-hover); }
        .tab-button.active { color: var(--color-primary); background: var(--color-background); }
        .tab-button.active::after { content: ''; position: absolute; bottom: 0; left: 0; right: 0; height: 2px; background: var(--color-primary); border-radius: var(--radius-full); transition: all var(--duration-fast) var(--easing-bounce); }
        .tab-button::before { content: ''; position: absolute; bottom: 0; left: 50%; right: 50%; height: 2px; background: var(--color-primary); border-radius: var(--radius-full); opacity: 0; transition: all var(--duration-normal) var(--easing-standard); }
        .tab-button:hover::before { left: 0; right: 0; opacity: 0.5; }
        
        .tab-button { display: inline-flex; align-items: center; justify-content: center; gap: 4px; }
        .tab-icon {
            display: inline-flex;
            align-items: center;
           )HTML" \
R"HTML( justify-content: center;
            flex: 0 0 13px;
            width: 13px;
            height: 13px;
            opacity: 0.9;
        }
        .tab-icon svg {
            display: block;
            width: 13px;
            height: 13px;
            overflow: visible;
        }
        .tab-icon svg * {
            fill: none;
            stroke: currentColor;
            stroke-width: 1.5;
            stroke-linecap: round;
            stroke-linejoin: round;
        }
        .tab-button.active .tab-icon { opacity: 1; }
        .content { 
            flex: 1; 
            padding: 0; 
            overflow: hidden; 
            background: var(--color-background); 
            display: flex;
            flex-direction: column;
        } 
        .tab-content { 
            display: none; 
            opacity: 0; 
            transform: translateY(3px); 
            transition: all var(--duration-normal) var(--easing-standard); 
            flex: 1;
            flex-direction: c)HTML" \
R"HTML(olumn;
            overflow-y: auto;
            padding: var(--spacing-lg); 
        } 
        #daily.tab-content {
            overflow-y: hidden;
            padding: 1px 4px 4px 4px;
        } 
        
        .tab-content:not(#daily) { padding: 12px 12px 12px 12px !important; }
        .tab-content:not(#daily) .section-title { font-size: 14px; margin-bottom: 12px; }
        .tab-content:not(#daily) .control-group { margin-bottom: 12px; }
        .tab-content:not(#daily) .label { font-size: 11px; margin-bottom: 5px; }
        .tab-content:not(#daily) .input { padding: 5px 8px; font-size: 11px; }
        .tab-content:not(#daily) .btn { padding: 5px 10px; font-size: 11px; }
        .tab-content:not(#daily) .list-item-title { font-size: 11px; }
        .tab-content:not(#daily) .list-item-subtitle { font-size: 9px; }
        .tab-content:not(#daily) .badge { font-size: 9px; }
        .tab-content:not(#daily) > div[style*="display: flex"] { margin-top: 0; }
        .tab-content.active)HTML" \
R"HTML( { display: flex; opacity: 1; transform: translateY(0); }
        .section-title { font-size: 14px; font-weight: 400; color: var(--color-text-primary); margin-bottom: var(--spacing-md); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards; } 
        .control-group { margin-bottom: var(--spacing-md); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards; } 
        .label { display: block; font-size: 11px; font-weight: 400; color: var(--color-text-primary); margin-bottom: var(--spacing-xs); } 
        .input { width: 100%; padding: var(--spacing-xs) var(--spacing-sm); border: 1px solid var(--color-border); border-radius: var(--radius-sm); font-size: 11px; background: var(--color-background); color: var(--color-text-primary); transition: all var(--duration-fast) var(--easing-standard); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var()HTML" \
R"HTML(--easing-enter) forwards; box-shadow: inset 0 1px 1px rgba(0, 0, 0, 0.05); } 
        .btn { padding: var(--spacing-xs) var(--spacing-md); border: 1px solid transparent; border-radius: var(--radius-sm); font-size: 11px; font-weight: 400; cursor: pointer; transition: all var(--duration-fast) var(--easing-standard); margin-right: var(--spacing-xs); margin-bottom: var(--spacing-xs); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards; box-shadow: none; } 
        .list-view { border: 1px solid var(--color-border); border-radius: var(--radius-sm); overflow: hidden; background: var(--color-background); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards; box-shadow: none; } 
        .list-item { padding: var(--spacing-sm) var(--spacing-md); border-bottom: 1px solid var(--color-border); transition: all var(--duration-fast) var(--easing-standard); cursor: pointer; display: f)HTML" \
R"HTML(lex; justify-content: space-between; align-items: center; opacity: 0; transform: translateX(-6px); animation: fadeInLeft var(--duration-normal) var(--easing-enter) forwards; } 
        .list-item:last-child { border-bottom: none; }
        .list-item:hover { background: var(--color-background-hover); transform: translateX(0); box-shadow: none; } 
        .list-item-content { flex: 1; }
        .list-item-title { font-weight: 400; color: var(--color-text-primary); margin-bottom: var(--spacing-xs); font-size: 11px; } 
        .list-item-subtitle { font-size: 9px; color: var(--color-text-secondary); } 
        .badge { padding: var(--spacing-xs) var(--spacing-sm); border-radius: var(--radius-sm); font-size: 9px; font-weight: 400; } 
        .badge-success { background: #e8f5e8; color: #2e7d32; } 
        .badge-warning { background: #fff8e1; color: #f57c00; } 
        .badge-danger { background: #ffebee; color: #c62828; } 
        .form-row { display: flex; gap: var(--spacing-sm); margin-)HTML" \
R"HTML(bottom: var(--spacing-md); opacity: 0; transform: translateY(5px); animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards; } 
        .form-col { flex: 1; }
        .divider { height: 1px; background: var(--color-border); margin: var(--spacing-md) 0; opacity: 0; animation: fadeIn var(--duration-normal) var(--easing-enter) forwards; } 
        
        
        @keyframes fadeIn { 
            from { opacity: 0; } 
            to { opacity: 1; } 
        }
        
        @keyframes fadeInUp { 
            from { opacity: 0; transform: translateY(6px); } 
            to { opacity: 1; transform: translateY(0); } 
        }
        
        @keyframes fadeInLeft { 
            from { opacity: 0; transform: translateX(-8px); } 
            to { opacity: 1; transform: translateX(0); } 
        }
        
        @keyframes slideUp { 
            from { opacity: 0; transform: translateY(-4px); } 
            to { opacity: 1; transform: translateY(0); } 
        }

        @)HTML" \
R"HTML(keyframes ambientDrift {
            0% { transform: translate3d(0, 0, 0) scale(1); }
            50% { transform: translate3d(-1.5%, -1.2%, 0) scale(1.025); }
            100% { transform: translate3d(1.2%, 1.5%, 0) scale(1.05); }
        }

        
        
        
        .panel-animate {
            opacity: 0;
            transform: translateY(4px);
            animation: fadeInUp var(--duration-normal) var(--easing-enter) forwards;
        }
        
        .panel-animate:nth-child(1) { animation-delay: 0.02s; }
        .panel-animate:nth-child(2) { animation-delay: 0.04s; }
        .panel-animate:nth-child(3) { animation-delay: 0.06s; }

        

        
        
        
        .input {
            transition: all 0.2s linear; 
        }
        .input:focus { 
            outline: none; 
            border-color: var(--color-primary); 
            box-shadow: 0 0 0 2px rgba(0, 120, 212, 0.3); 
            transform: translateY(0); 
        }
        .input:hover { 
     )HTML" \
R"HTML(       border-color: var(--color-primary); 
            transform: translateY(0); 
        }
        .btn-primary {
            background: linear-gradient(180deg, var(--color-primary) 0%, var(--color-primary-hover) 100%);
            color: #ffffff;
            border-color: var(--color-primary);
            transition: transform var(--duration-fast) var(--easing-standard), box-shadow var(--duration-fast) var(--easing-standard), background-color 0.1s linear, border-color 0.1s linear;
            box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.14);
        }
        .btn-primary:hover {
            background: linear-gradient(180deg, var(--color-primary-hover) 0%, #0d64b8 100%);
            border-color: var(--color-primary-hover);
            transform: translateY(-1px);
            box-shadow: 0 4px 12px rgba(0, 120, 212, 0.18), inset 0 1px 0 rgba(255, 255, 255, 0.12);
        }
        .btn-primary:active {
            transform: translateY(0);
            box-shadow: 0 2px 6px rgba)HTML" \
R"HTML((0, 120, 212, 0.12);
        }
        .btn-secondary {
            background: linear-gradient(180deg, var(--color-background) 0%, #fbfbfb 100%);
            color: var(--color-text-primary);
            border-color: var(--color-border);
            transition: transform var(--duration-fast) var(--easing-standard), box-shadow var(--duration-fast) var(--easing-standard), background-color 0.1s linear, border-color 0.1s linear;
        }
        .btn-secondary:hover {
            background: linear-gradient(180deg, var(--color-background-secondary) 0%, #f6f6f6 100%);
            border-color: var(--color-primary);
            transform: translateY(-1px);
            box-shadow: 0 3px 10px rgba(0, 0, 0, 0.06);
        }
        .btn-secondary:active {
            transform: translateY(0);
            box-shadow: none;
        }
        
        
        ::-webkit-scrollbar { width: 6px; } 
        ::-webkit-scrollbar-track { background: var(--color-background); } 
        ::-webkit-scrol)HTML" \
R"HTML(lbar-thumb { background: var(--color-border); border-radius: var(--radius-full); transition: all var(--duration-fast) var(--easing-standard); } 
        ::-webkit-scrollbar-thumb:hover { background: var(--color-border-hover); } 
        
        
        #my-buf-list,
        #other-buf-list {
            overflow-x: hidden;
            overflow-y: hidden;
            overflow: hidden;
        }
        
        
        #my-buf-list.overflow-auto,
        #other-buf-list.overflow-auto {
            overflow-x: auto !important;
            overflow-y: hidden !important;
        }
        
        
        #my-buf-list::-webkit-scrollbar,
        #other-buf-list::-webkit-scrollbar {
            width: 6px;
            height: 6px;
            display: none;
        }
        
        
        #my-buf-list.overflow-auto::-webkit-scrollbar,
        #other-buf-list.overflow-auto::-webkit-scrollbar {
            display: block !important;
        }
        
        #my-buf-list::-webkit-scr)HTML" \
R"HTML(ollbar-track,
        #other-buf-list::-webkit-scrollbar-track {
            background: #e0e0e0;
            border-radius: 4px;
        }
        
        #my-buf-list::-webkit-scrollbar-thumb,
        #other-buf-list::-webkit-scrollbar-thumb {
            background: #888;
            border-radius: 4px;
        }
        
        #my-buf-list::-webkit-scrollbar-thumb:hover,
        #other-buf-list::-webkit-scrollbar-thumb:hover {
            background: #555;
        }
        
        .skill-row {
            display: grid;
            grid-template-columns: 105px 75px;
            padding: 3px 5px;
            border-bottom: 1px solid var(--color-border);
            height: 20px;
            line-height: 14px;
            overflow: hidden;
        }
        
        .skill-row > div {
            font-size: 9.5px;
        }

        
        .sub-tabs {
            display: flex;
            background: var(--color-background-secondary);
            border-bottom: 1px solid var()HTML" \
R"HTML(--color-border);
            padding: 2px;
            gap: 4px;
            margin-bottom: 6px;
            border-radius: var(--radius-sm);
            justify-content: flex-start;
        }
        
        .sub-tab-button {
            width: 75px;
            height: 22px;
            display: flex;
            align-items: center;
            justify-content: center;
            border: none;
            background: transparent;
            color: var(--color-text-secondary);
            font-size: 10px;
            font-weight: 400;
            cursor: pointer;
            transition: all var(--duration-fast) var(--easing-standard);
            border-radius: var(--radius-sm);
        }
        
        .sub-tab-button:hover {
            background: var(--color-background-hover);
            color: var(--color-primary);
        }
        
        .sub-tab-button.active {
            background: linear-gradient(180deg, var(--color-primary) 0%, var(--color-primary-hover) 100%);
 )HTML" \
R"HTML(           color: white;
            box-shadow: 0 3px 8px rgba(0, 120, 212, 0.16);
        }
        
        .sub-tab-content {
            display: none;
            flex: 1;
            overflow-y: auto;
            overflow-x: visible;
            padding-right: 2px; 
        }
        
        .sub-tab-content.active {
            display: block;
            animation: fadeIn var(--duration-fast) var(--easing-enter);
        }
        
        
        .tab-content.active .sub-tab-content.active {
            display: flex;
            flex-direction: column;
        }

        
        .fx-page {
            display: flex;
            flex-direction: column;
            gap: 4px;
            flex: 1;
            min-height: 0;
            padding: 3px 6px;
        }
        
        .fx-row {
            display: flex;
            gap: 8px;
            align-items: stretch;
            flex: 1;
            min-height: 0;
        }
        
        .fx-panel {
            display)HTML" \
R"HTML(: flex;
            flex-direction: column;
            gap: 3px;
            min-height: 0;
            min-width: 0;
            background: var(--color-background);
            border: 1px solid var(--color-border);
            border-radius: 4px;
            padding: 5px 6px;
            box-shadow: var(--shadow-sm);
            overflow: hidden;
        }
        .fx-panel-grow { flex: 1 1 auto; }
        .fx-panel-title {
            font-size: 11px;
            font-weight: 600;
            color: var(--color-text-primary);
            line-height: 1.2;
            flex: 0 0 auto;
        }
        .fx-panel-note {
            font-size: 8.5px;
            color: var(--color-text-tertiary);
            line-height: 1.35;
            flex: 0 0 auto;
        }
        
        .fx-scroll {
            flex: 1 1 auto;
            min-height: 0;
            overflow-y: auto;
            overflow-x: hidden;
        }
        
        .fx-actions {
            display: flex;
         )HTML" \
R"HTML(   gap: 4px;
            flex: 0 0 auto;
            margin-top: auto;
        }
        .fx-actions > .btn { margin: 0; }
        
        .fx-checks {
            display: grid;
            gap: 2px 4px;
            align-content: start;
        }
        .fx-check {
            display: flex;
            align-items: center;
            gap: 3px;
            padding: 1px 4px;
            background: #FFFFFF;
            border-radius: 2px;
            cursor: pointer;
            font-size: 9.5px;
            color: var(--color-text-primary);
            white-space: nowrap;
        }
        
        .fx-sep {
            width: 1px;
            background: var(--color-border);
            flex: 0 0 auto;
        }
        
        .act-item {
            display: flex;
            align-items: center;
            gap: 5px;
            min-width: 0;
            padding: 2px 6px;
            background: #FFFFFF;
            border: 1px solid #D9E4F1;
            border-radius: 4px;
)HTML" \
R"HTML(            cursor: pointer;
            font-size: 10px;
            color: var(--color-text-primary);
            transition: border-color var(--duration-fast) var(--easing-standard), background var(--duration-fast) var(--easing-standard);
        }
        .act-item:hover { border-color: var(--color-border-hover); background: var(--color-background-hover); }
        
        .act-item.act-disabled {
            opacity: 0.55;
            cursor: not-allowed;
            background: var(--color-background-secondary);
        }
        .act-item.act-disabled .act-item-name { color: var(--color-text-tertiary); }
        .act-item > input[type="checkbox"].act-pick {
            flex: 0 0 auto;
            width: 12px;
            height: 12px;
            cursor: pointer;
        }
        .act-item-name {
            flex: 1 1 auto;
            min-width: 0;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }
        .act-item-c)HTML" \
R"HTML(ount {
            flex: 0 0 auto;
            min-width: 30px;
            text-align: center;
            font-size: 9px;
            font-weight: 600;
            color: var(--color-primary);
            background: #e8f4fd;
            border-radius: 3px;
            padding: 0 4px;
            line-height: 1.5;
        }
        .act-item-count.act-count-zero {
            color: var(--color-text-tertiary);
            background: var(--color-background-secondary);
        }
        .act-item-count.act-count-unknown {
            color: var(--color-text-tertiary);
            background: transparent;
        }

        .btn,
        .sub-tab-button,
        .tab-action-button {
            position: relative;
            overflow: hidden;
            will-change: transform, box-shadow, background-color, border-color;
            transform: translateZ(0);
        }

        .tab-button {
            will-change: transform, box-shadow, background-color, border-color;
        }

    )HTML" \
R"HTML(    .btn:hover,
        .sub-tab-button:hover,
        .tab-action-button:hover,
        .tab-button:hover {
            transform: translateY(-1px);
        }

        .btn:active,
        .sub-tab-button:active,
        .tab-action-button:active,
        .tab-button:active {
            transform: translateY(0);
        }

        
        #sub-spiritcollect {
            padding: 4px 6px;
            box-sizing: border-box;
            height: 100%;
            min-height: 0;
            overflow: hidden;
        }

        .tab-content.active #sub-spiritcollect.active {
            display: flex;
            flex-direction: column;
        }

        #sub-spiritcollect .spirit-collect-layout {
            flex: 1;
            min-height: 0;
            display: flex;
            flex-direction: column;
            gap: 5px;
            overflow: hidden;
        }

        
        .spirit-toolbar {
            flex: 0 0 auto;
            display: flex;
            align-items: cent)HTML" \
R"HTML(er;
            gap: 6px;
            min-width: 0;
            height: 26px;
            overflow: hidden;
        }

        .spirit-toolbar .spirit-stat-chip {
            flex: 0 0 auto;
        }

        .spirit-toolbar-selected {
            flex: 1 1 auto;
            min-width: 0;
            font-size: 9.5px;
            color: var(--color-text-secondary);
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-toolbar-send {
            flex: 0 0 auto;
            display: flex;
            align-items: center;
            gap: 4px;
        }

        .spirit-toolbar-send label {
            font-size: 9.5px;
            color: var(--color-text-secondary);
            white-space: nowrap;
        }

        .spirit-toolbar-send input {
            width: 96px;
            height: 22px;
            border-radius: 5px;
            border: 1px solid var(--color-border);
            font-size: 9.5px;
        )HTML" \
R"HTML(    padding: 2px 6px;
            box-sizing: border-box;
        }

        .spirit-toolbar-send .btn {
            font-size: 9.5px;
            padding: 3px 10px;
            min-height: 22px;
            margin: 0;
            line-height: 1.1;
        }

        .spirit-toolbar .spirit-status-line {
            flex: 0 1 auto;
            max-width: 200px;
        }

        .spirit-toolbar > .btn {
            flex: 0 0 auto;
            font-size: 9.5px;
            padding: 3px 10px;
            min-height: 22px;
            margin: 0;
            line-height: 1.1;
        }

        
        #sub-spiritcollect .spirit-columns {
            flex: 1 1 auto;
            min-height: 0;
            display: flex;
            gap: 8px;
            align-items: stretch;
            overflow: hidden;
        }

        .spirit-giving-panel,
        .spirit-record-side {
            display: flex;
            flex-direction: column;
            gap: 4px;
            min-width: 0;
     )HTML" \
R"HTML(       min-height: 0;
            background: var(--color-background);
            border: 1px solid var(--color-border);
            border-radius: 6px;
            padding: 6px 7px;
            box-shadow: var(--shadow-sm);
            overflow: hidden;
        }

        .spirit-giving-panel {
            flex: 1.25;
        }

        .spirit-giving-actions {
            display: flex;
            flex-wrap: nowrap;
            gap: 4px;
            flex: 0 0 auto;
        }

        .spirit-giving-actions .btn {
            font-size: 8.5px;
            padding: 2px 6px;
            min-height: 20px;
            margin: 0;
            line-height: 1.1;
        }

        .spirit-stat-chip {
            display: inline-flex;
            align-items: center;
            gap: 4px;
            padding: 2px 6px;
            background: #f5f8fc;
            border: 1px solid #dbe7f2;
            border-radius: 6px;
            white-space: nowrap;
        }

        
        #spirit-lis)HTML" \
R"HTML(t {
            flex: 1 1 auto;
            min-height: 0;
            overflow-y: auto;
            border: 1px solid var(--color-border);
            border-radius: 6px;
            padding: 3px;
            background: linear-gradient(180deg, #ffffff 0%, #f7fbff 100%);
            font-size: 10px;
            scrollbar-gutter: stable;
        }

        #sub-spiritcollect.sub-tab-content {
            overflow: hidden;
            padding-right: 0;
        }

        .spirit-list-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(190px, 1fr));
            gap: 3px;
            align-content: start;
        }

        .spirit-list-empty {
            color: var(--color-text-tertiary);
            text-align: center;
            padding: 10px 8px;
        }

        
        .spirit-item {
            position: relative;
            padding: 3px 6px;
            cursor: pointer;
            border-radius: 6px;
            border: 1px solid #d)HTML" \
R"HTML(9e4ef;
            background: linear-gradient(180deg, #fbfdff 0%, #f1f6fb 100%);
            transition: transform var(--duration-fast) var(--easing-standard), box-shadow var(--duration-fast) var(--easing-standard), border-color var(--duration-fast) var(--easing-standard), background var(--duration-fast) var(--easing-standard);
            min-width: 0;
            overflow: hidden;
        }

        .spirit-item:hover {
            transform: translateY(-1px);
            border-color: #b8d3ea;
            box-shadow: 0 4px 10px rgba(0, 120, 212, 0.12);
        }

        .spirit-item.selected {
            border-color: #6fb6f0;
            background: linear-gradient(135deg, #eaf6ff 0%, #dcefff 100%);
            box-shadow: 0 6px 14px rgba(0, 120, 212, 0.18);
        }

        .spirit-item.selected::after {
            content: '';
            position: absolute;
            inset: 0;
            border-radius: inherit;
            box-shadow: inset 0 0 0 1px rgba(255, 255, 255,)HTML" \
R"HTML( 0.7);
            pointer-events: none;
        }

        
        .spirit-item-top {
            display: flex;
            align-items: center;
            gap: 6px;
            min-width: 0;
        }

        .spirit-item-name {
            flex: 0 1 auto;
            min-width: 0;
            font-size: 9.5px;
            font-weight: 700;
            color: var(--color-text-primary);
            line-height: 1.2;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-item-tag {
            flex: 0 0 auto;
            padding: 0 5px;
            border-radius: 999px;
            background: rgba(0, 120, 212, 0.1);
            color: var(--color-primary);
            font-weight: 700;
            font-size: 8.5px;
            line-height: 1.3;
            white-space: nowrap;
        }

        .spirit-item-chips {
            display: flex;
            flex-wrap: nowrap;
            gap: 3px;
            min)HTML" \
R"HTML(-width: 0;
            overflow: hidden;
        }

        .spirit-mini-chip {
            display: inline-flex;
            align-items: center;
            min-width: 0;
            max-width: 100%;
            padding: 0 4px;
            border-radius: 999px;
            background: rgba(0, 0, 0, 0.04);
            color: var(--color-text-secondary);
            font-size: 8.5px;
            line-height: 1.3;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-item-detail {
            display: flex;
            flex-direction: column;
            gap: 1px;
            font-size: 8.5px;
            color: var(--color-text-secondary);
            line-height: 1.3;
        }

        .spirit-item-detail-line {
            min-width: 0;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-status-line {
            font-size: 9.5px;
      )HTML" \
R"HTML(      color: var(--color-text-tertiary);
            padding: 0 2px;
            line-height: 1.25;
            flex: 0 1 auto;
            min-width: 0;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-record-side {
            flex: 1;
            min-width: 0;
            min-height: 0;
            gap: 3px;
        }

        .spirit-record-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 6px;
            flex: 0 0 auto;
        }

        .spirit-record-title {
            font-size: 10.5px;
            font-weight: 700;
            color: var(--color-text-primary);
        }

        
        .spirit-record-meta {
            display: flex;
            align-items: center;
            gap: 6px;
            flex: 0 0 auto;
            min-width: 0;
        }

        .spirit-record-subtitle {
            font-size: 9.5px;
  )HTML" \
R"HTML(          color: var(--color-text-tertiary);
            flex: 1 1 auto;
            min-width: 0;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .spirit-record-body {
            flex: 1 1 auto;
            min-height: 0;
            overflow-y: auto;
            background: linear-gradient(180deg, #fffdf8 0%, #f8fbff 100%);
            border: 1px solid var(--color-border);
            border-radius: 6px;
            padding: 4px 6px;
            font-size: 10px;
            line-height: 1.35;
            color: var(--color-text-secondary);
            white-space: pre-wrap;
            word-break: break-word;
        }

        @media (max-width: 960px) {
            .spirit-list-grid {
                grid-template-columns: repeat(2, minmax(0, 1fr));
            }

            .spirit-toolbar {
                height: auto;
                flex-wrap: wrap;
            }

            .spirit-toolbar-send input {)HTML" \
R"HTML(
                width: 80px;
            }
        }

        @media (max-width: 760px) {
            .spirit-list-grid {
                grid-template-columns: minmax(0, 1fr);
            }

            #sub-spiritcollect .spirit-columns {
                flex-direction: column;
            }

            .spirit-toolbar-send input {
                width: 100%;
            }
        }

        
        .item-btn {
            padding: 2px 5px;
            font-size: 8px;
            background: #fafafa;
            color: #333;
            border: 1px solid #e0e0e0;
            border-radius: 3px;
            cursor: pointer;
            transition: all 0.15s ease;
            white-space: nowrap;
        }
        
        .item-btn:hover {
            background: #f0f0f0;
            border-color: #bdbdbd;
        }
        
        .item-btn.selected {
            background: #e3f2fd;
            color: #1976d2;
            border-color: #1976d2;
            font-weight: 600;
   )HTML" \
R"HTML(     }

        
        .packet-list {
            overflow-y: auto;
            height: calc(100% - 64px);
            padding: 4px;
            border-bottom: 1px solid var(--color-border);
        }
        
        .packet-item {
            padding: 2.8px;
            border-bottom: 1px solid var(--color-border);
            font-size: 7px;
            cursor: pointer;
            transition: none;
            border-radius: 1.4px;
        }
        
        .packet-item:hover {
            background: var(--color-background-hover);
        }
        
        .packet-item.selected {
            background: rgba(0, 120, 212, 0.1);
            border-left: 2.1px solid var(--color-primary);
        }
        
        .packet-send-container {
            padding: 2.8px;
            display: flex;
            gap: 2.8px;
            align-items: center;
            background: var(--color-background-secondary);
        }
        
        .packet-hex-input {
            flex: 1;
      )HTML" \
R"HTML(      font-size: 9px;
            padding: 3px 6px;
            border: 1px solid var(--color-border);
            border-radius: var(--radius-sm);
            transition: all 0.2s linear;
        }
        
        .packet-hex-input:focus {
            outline: none;
            border-color: var(--color-primary);
            box-shadow: 0 0 0 2px rgba(0, 120, 212, 0.3);
        }
        
        .send-packet-btn {
            padding: 3px 8px;
            font-size: 9px;
            background: var(--color-primary);
            color: white;
            border: none;
            border-radius: var(--radius-sm);
            cursor: pointer;
            transition: all 0.2s linear;
        }
        
        .send-packet-btn:hover {
            background: var(--color-primary-hover);
        }

        
        .update-dialog-overlay {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: rgba(0, 0, 0)HTML" \
R"HTML(, 0.3);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 999999;
            opacity: 0;
            visibility: hidden;
            transition: opacity 0.2s ease;
        }

        .update-dialog-overlay.show {
            opacity: 1;
            visibility: visible;
        }

        .update-dialog {
            background: #ffffff;
            border-radius: 8px;
            box-shadow: 0 4px 20px rgba(0, 0, 0, 0.15);
            width: 380px;
            max-width: 90%;
            overflow: hidden;
        }

        .update-dialog-header {
            background: #f8fafc;
            color: #334155;
            padding: 16px 18px;
            font-size: 13px;
            font-weight: 600;
            display: flex;
            align-items: center;
            gap: 10px;
            border-bottom: 1px solid #e2e8f0;
        }

        .update-dialog-header-icon {
            width: 28px;
            height: 28p)HTML" \
R"HTML(x;
            background: #3b82f6;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
            flex-shrink: 0;
        }

        .update-dialog-header-icon svg {
            width: 16px;
            height: 16px;
            fill: none;
            stroke: white;
            stroke-width: 2;
        }

        .update-dialog-body {
            padding: 18px;
            max-height: 300px;
            overflow-y: auto;
            background: #ffffff;
        }

        .update-dialog-announcement {
            font-size: 12px;
            font-weight: 600;
            color: #1e293b;
            margin-bottom: 12px;
            padding: 10px 12px;
            background: #eff6ff;
            border-radius: 6px;
            border-left: 3px solid #3b82f6;
        }

        .update-dialog-content {
            font-size: 11px;
            color: #64748b;
            line-height: 1.6;
            white-spa)HTML" \
R"HTML(ce: pre-wrap;
        }

        .update-dialog-footer {
            padding: 14px 18px;
            background: #f8fafc;
            display: flex;
            justify-content: flex-end;
            gap: 8px;
            border-top: 1px solid #e2e8f0;
        }

        .update-dialog-btn {
            padding: 8px 18px;
            border-radius: 4px;
            font-size: 11px;
            font-weight: 500;
            cursor: pointer;
            transition: background 0.15s ease;
        }

        .update-dialog-btn.primary {
            background: #3b82f6;
            color: white;
            border: none;
        }

        .update-dialog-btn.primary:hover {
            background: #2563eb;
        }

        .update-dialog-btn.secondary {
            background: transparent;
            color: #64748b;
            border: 1px solid #e2e8f0;
        }

        .update-dialog-btn.secondary:hover {
            background: #f1f5f9;
        }
        .tab-button,
        .sub-ta)HTML" \
R"HTML(b-button,
        .tab-action-button,
        .btn {
            position: relative;
            overflow: hidden;
            will-change: transform, box-shadow, background-color, border-color;
        }

        .tab-button:hover,
        .sub-tab-button:hover,
        .tab-action-button:hover,
        .btn:hover {
            transform: translateY(-1px);
        }

        .tab-button:active,
        .sub-tab-button:active,
        .tab-action-button:active,
        .btn:active {
            transform: translateY(0);
        }

        .tab-button.active {
            box-shadow: inset 0 -2px 0 rgba(0, 120, 212, 0.18), 0 2px 8px rgba(0, 120, 212, 0.08);
        }

        .sub-tab-button.active {
            box-shadow: 0 3px 8px rgba(0, 120, 212, 0.16);
        }

        #wpe button {
            position: relative;
            will-change: transform, box-shadow, filter;
            transform: translateZ(0);
            transition: transform var(--duration-fast) var(--easing-stand)HTML" \
R"HTML(ard), box-shadow var(--duration-fast) var(--easing-standard), filter var(--duration-fast) var(--easing-standard);
        }

        #wpe button:hover {
            transform: translateY(-1px);
            box-shadow: 0 3px 10px rgba(0, 0, 0, 0.08);
            filter: brightness(1.02);
        }

        #wpe button:active {
            transform: translateY(0);
            box-shadow: none;
            filter: brightness(0.98);
        }

        @media (prefers-reduced-motion: reduce) {
            *, *::before, *::after {
                animation-duration: 0.01ms !important;
                animation-iteration-count: 1 !important;
                transition-duration: 0.01ms !important;
                scroll-behavior: auto !important;
            }

            body::before {
                animation: none !important;
            }
        }
    </style>
</head>
<body>

<div style="position:absolute;top:32px;left:0;right:0;height:566px;display:flex;align-items:center;
justify-con)HTML" \
R"HTML(tent:center;background:#eef3fa;border-bottom:1px solid #e0e0e0;color:#6e6e6e;font-size:12px;">
游戏画面区域（基线对照 · 改版前）</div>

    
    <div class="title-bar">
        <div class="title-bar-left">
            <div class="title-bar-icon">卡</div>
            <span class="title-bar-title">卡布西游浮影微端 V1.16</span>
        </div>
        <div class="title-bar-controls">
            <button class="title-bar-button" id="btn-minimize" title="最小化">
                <svg viewBox="0 0 12 12">
                    <rect x="2" y="5.5" width="8" height="1" fill="currentColor"/>
                </svg>
            </button>
            <button class="title-bar-button close" id="btn-close" title="关闭">
                <svg viewBox="0 0 12 12">
                    <path d="M2 2L10 10M10 2L2 10" stroke="currentColor" stroke-width="1.5" fill="none"/>
                </svg>
            </button>
        </div>
    </div>

    
    
    <div class="helper-info">
        <span class="helper-label">辅助提示:</span>
        <)HTML" \
R"HTML(span class="helper-text" id="helper-text">当前暂时没有操作</span>
    </div>

    
    <div class="update-dialog-overlay" id="update-dialog-overlay">
        <div class="update-dialog">
            <div class="update-dialog-header">
                <div class="update-dialog-header-icon">
                    <svg viewBox="0 0 24 24">
                        <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
                        <polyline points="7 10 12 15 17 10"/>
                        <line x1="12" y1="15" x2="12" y2="3"/>
                    </svg>
                </div>
                <span>发现新版本</span>
            </div>
            <div class="update-dialog-body">
                <div class="update-dialog-announcement" id="update-dialog-announcement"></div>
                <div class="update-dialog-content" id="update-dialog-content"></div>
            </div>
            <div class="update-dialog-footer">
                <button class="update-dialog-btn secondary" id="update-dial)HTML" \
R"HTML(og-cancel">稍后提醒</button>
                <button class="update-dialog-btn primary" id="update-dialog-confirm">立即更新</button>
            </div>
        </div>
    </div>

    
    <div class="update-dialog-overlay" id="key-login-overlay">
        <div class="update-dialog">
            <div class="update-dialog-header">
                <div class="update-dialog-header-icon">
                    <svg viewBox="0 0 24 24">
                        <path d="M8 1a4 4 0 0 0-4 4 4 4 0 0 0 3.5 3.97V11h-1a1 1 0 0 0-1 1v2a1 1 0 0 0 1 1h2a1 1 0 0 0 1-1v-2.03A4 4 0 0 0 12 5a4 4 0 0 0-4-4zm0 1a3 3 0 0 1 3 3 3 3 0 0 1-3 3 3 3 0 0 1-3-3 3 3 0 0 1 3-3z"/>
                    </svg>
                </div>
                <span>Key 登录</span>
            </div>
            <div class="update-dialog-body">
                <div class="update-dialog-content">
                    <textarea id="key-login-input" placeholder="粘贴登录 Key..." style="width: 100%; min-height: 120px; padding: 12px; border: 2px solid var)HTML" \
R"HTML((--color-border); border-radius: var(--radius-md); font-family: 'Consolas', 'Monaco', 'Courier New', monospace; font-size: 12px; line-height: 1.5; color: var(--color-text-primary); background: var(--color-background); resize: vertical; transition: all var(--duration-fast) var(--easing-standard); outline: none;"></textarea>
                </div>
            </div>
            <div class="update-dialog-footer">
                <button class="update-dialog-btn secondary" id="key-login-cancel">取消</button>
                <button class="update-dialog-btn primary" id="key-login-confirm">登录</button>
            </div>
        </div>
    </div>

    <div class="update-dialog-overlay" id="spirit-confirm-overlay">
        <div class="update-dialog">
            <div class="update-dialog-header">
                <div class="update-dialog-header-icon">
                    <svg viewBox="0 0 24 24">
                        <path d="M12 2L2 7l10 5 10-5-10-5z"/>
                        <path d="M2 17)HTML" \
R"HTML(l10 5 10-5"/>
                        <path d="M2 12l10 5 10-5"/>
                    </svg>
                </div>
                <span>确认赠送</span>
            </div>
            <div class="update-dialog-body">
                <div class="update-dialog-announcement" id="spirit-confirm-title">精魄赠送确认</div>
                <div class="update-dialog-content" id="spirit-confirm-message"></div>
            </div>
            <div class="update-dialog-footer">
                <button class="update-dialog-btn secondary" id="spirit-confirm-cancel">取消</button>
                <button class="update-dialog-btn primary" id="spirit-confirm-accept">确认赠送</button>
            </div>
        </div>
    </div>

    <div class="container">
        
        
        <div id="packet-window" style="position: absolute; top: -294px; left: 0; width: 420px; height: 294px; background: var(--color-background); border: 1px solid var(--color-border); border-radius: var(--radius-md); box-shadow: var(--shadow-xl); )HTML" \
R"HTML(z-index: 9999; display: none; opacity: 0; transform: translateY(0); transition: opacity 0.2s ease, transform 0.2s ease; border-top: 3px solid var(--color-primary);">
            <div style="display: flex; justify-content: space-between; align-items: center; padding: 5.6px 11.2px; background: var(--color-background-secondary); border-bottom: 1px solid var(--color-border); border-radius: var(--radius-md) var(--radius-md) 0 0;">
                <h3 style="font-size: 11.2px; font-weight: 600; color: var(--color-text-primary); margin: 0;">封包列表</h3>
                <div style="display: flex; gap: 2.8px;">
                    <button id="delete-selected-packets" style="padding: 2.8px 5.6px; font-size: 8.4px; background: #ffebee; color: #c62828; border: 1px solid #ffcdd2; border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">删除选中</button>
                    <button id="close-packet-window" style="padding: 2.8px 5.6px; font-size: 8.4px;)HTML" \
R"HTML( background: var(--color-primary); color: white; border: none; border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">关闭</button>
                </div>
            </div>
            <div class="packet-list" id="packet-list" style="display: flex; flex-direction: column;">
                
                <div id="packet-list-items" style="flex: 1; overflow-y: auto; overflow-x: hidden; padding: 2.8px;">
                    
                </div>
            </div>
            
            <div class="packet-send-container" style="padding: 5.6px; gap: 4.2px;">
                <input type="text" class="packet-hex-input" id="packet-hex-input" placeholder="选中封包后显示16进制字符串，可编辑后发送" style="font-size: 8.4px; padding: 2.8px 5.6px;">
                <button class="send-packet-btn" id="send-packet-btn" style="padding: 2.8px 8.4px; font-size: 8.4px;">发送</button>
            </div>
        </div>
        
        <div class="tabs">
          )HTML" \
R"HTML(  <button class="tab-button active" onclick="openTab(event, 'battle')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><path d="M8 2v7.8"/><path d="M5.2 9.8h5.6"/><path d="M8 9.8V13"/><path d="M6.9 13h2.2"/></svg></span><span>战斗数据</span></button>
            <button class="tab-button" onclick="openTab(event, 'monster')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><path d="M4.6 5.6h6.8A1.6 1.6 0 0 1 13 7.2v4.8a1.6 1.6 0 0 1-1.6 1.6H4.6A1.6 1.6 0 0 1 3 12V7.2a1.6 1.6 0 0 1 1.6-1.6z"/><path d="M6.2 5.6V4.4a1.8 1.8 0 0 1 3.6 0v1.2"/><path d="M6 9.6h4"/></svg></span><span>妖怪背包</span></button>
            <button class="tab-button" onclick="openTab(event, 'daily')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><rect x="3" y="3" width="4.2" height="4.2" rx="1.1"/><rect x="8.8" y="3" width="4.2" height="4.2" rx="1.1"/><rect x="3" y="8.8" width="4.2" height="4.2" rx="1.1"/><rect x="8.8)HTML" \
R"HTML(" y="8.8" width="4.2" height="4.2" rx="1.1"/></svg></span><span>功能区</span></button>
            <button class="tab-button" onclick="openTab(event, 'wpe')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><circle cx="8" cy="3.6" r="1.5"/><circle cx="3.6" cy="12.2" r="1.5"/><circle cx="12.4" cy="12.2" r="1.5"/><path d="M7.2 4.9L4.4 10.8"/><path d="M8.8 4.9l2.8 5.9"/><path d="M5.1 12.2h5.8"/></svg></span><span>WPE</span></button>
            <button class="tab-button" onclick="openTab(event, 'settings')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg"><path d="M2.6 4.6h10.8"/><path d="M2.6 8h10.8"/><path d="M2.6 11.4h10.8"/><circle cx="6" cy="4.6" r="1.3"/><circle cx="10.2" cy="8" r="1.3"/><circle cx="5.4" cy="11.4" r="1.3"/></svg></span><span>设置</span></button>
            <button class="tab-button" onclick="openTab(event, 'helper-log')"><span class="tab-icon"><svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">)HTML" \
R"HTML(<path d="M4.4 2.4h4.9l2.9 2.9v8.3a.9.9 0 0 1-.9.9H4.4a.9.9 0 0 1-.9-.9V3.3a.9.9 0 0 1 .9-.9z"/><path d="M9.3 2.4v2.9h2.9"/><path d="M5.9 8.7h4.2"/><path d="M5.9 11.2h4.2"/></svg></span><span>辅助日志</span></button>

            
            <div class="tab-actions">
                
                <button class="tab-action-button" id="copy-key" title="复制登录Key">
                    <span class="icon-copy">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <rect x="5.4" y="5.4" width="8.2" height="8.2" rx="1.6"/>
                            <path d="M10.6 5.4V4.4A1.6 1.6 0 0 0 9 2.8H4.4A1.6 1.6 0 0 0 2.8 4.4V9A1.6 1.6 0 0 0 4.4 10.6h1"/>
                        </svg>
                    </span>
                    <span class="tab-action-label">复制Key</span>
                </button>

                
                <button class="tab-action-button" id="key-login" title="使用Key登录">
                    <span class="icon-key">
  )HTML" \
R"HTML(                      <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <circle cx="5.3" cy="8" r="2.7"/>
                            <path d="M8 8h5.6"/>
                            <path d="M11.2 8v2.3"/>
                            <path d="M13.6 8v1.7"/>
                        </svg>
                    </span>
                    <span class="tab-action-label">Key登录</span>
                </button>

                
                <button class="tab-action-button" id="refresh-game" title="刷新游戏">
                    <span class="icon-refresh">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <path d="M13 8A5 5 0 1 1 10.9 3.9"/>
                            <path d="M13.2 2.7v3.1h-3.1"/>
                        </svg>
                    </span>
                    <span class="tab-action-label">刷新游戏</span>
                </button>

                
                <button class=)HTML" \
R"HTML("tab-action-button" id="refresh-no-login" title="不换号刷新">
                    <span class="icon-refresh-nologin">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <path d="M11.6 6.9a4.3 4.3 0 1 0 .3 2.6"/>
                            <path d="M12.3 3.4v3.1h-3.1"/>
                            <path d="M10.9 9.3h2.4a.8.8 0 0 1 .8.8v1.7a.8.8 0 0 1-.8.8h-2.4a.8.8 0 0 1-.8-.8v-1.7a.8.8 0 0 1 .8-.8z"/>
                            <path d="M11 9.3v-.8a1.1 1.1 0 0 1 2.2 0v.8"/>
                        </svg>
                    </span>
                    <span class="tab-action-label">不换号刷新</span>
                </button>
                
                
                <button class="tab-action-button" id="mute-game" title="游戏静音/恢复">
                    <span class="icon-mute">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <path d="M2.7 6.4h2.2L8.4 3.5v9l-3.5-2.)HTML" \
R"HTML(9H2.7z"/>
                            <g class="mute-wave">
                                <path d="M10.6 6.3a2.5 2.5 0 0 1 0 3.4"/>
                                <path d="M12.5 4.9a4.6 4.6 0 0 1 0 6.2"/>
                            </g>
                            <path class="mute-slash" d="M10.2 5.7l4 4.6"/>
                        </svg>
                    </span>
                    <span class="tab-action-label" id="mute-text">游戏静音</span>
                </button>

                
                <button class="tab-action-button" id="clear-ie-cache" title="清理 IE 浏览器缓存">
                    <span class="icon-clear-cache">
                        <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
                            <path d="M3.1 4.9h9.8"/>
                            <path d="M6.3 4.9V3.5a.9.9 0 0 1 .9-.9h1.6a.9.9 0 0 1 .9.9v1.4"/>
                            <path d="M4.5 4.9l.6 7.3a1 1 0 0 0 1 .9h3.8a1 1 0 0 0 1-.9l.6-7.3"/>
                            <pa)HTML" \
R"HTML(th d="M6.8 7.3v3.8"/>
                            <path d="M9.2 7.3v3.8"/>
                        </svg>
                    </span>
                    <span class="tab-action-label" id="clear-cache-text">清理缓存</span>
                </button>
            </div>
        </div>
        <div class="content">
            
            <div id="battle" class="tab-content active">
                
                <div style="display: flex; gap: 12px; margin-bottom: 12px; justify-content: flex-start; align-items: stretch;">
                    
                    <div style="width: 320px; height: 150px; opacity: 0; transform: translateY(4px); animation: fadeInUp var(--duration-slow) var(--easing-enter) forwards; animation-delay: 0.1s; flex: none;">
                        <div style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; box-shadow: var(--shadow-sm); height: 100%; display: flex; flex-direction: column;">
               )HTML" \
R"HTML(             <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px; min-height: 18px;">
                                <h3 style="font-size: 11.5px; font-weight: 600; color: var(--color-text-primary); margin: 0;">我方数据</h3>
                                
                                <div id="my-buf-list" style="display: flex; flex-wrap: nowrap; gap: 2px; max-width: 200px; min-width: 0; overflow-x: hidden; overflow-y: hidden; padding-bottom: 4px;">
                                    
                                </div>
                            </div>
                            
                            
                            <div style="display: flex; gap: 8px; flex: 1;">
                                
                                <div style="flex: 0 0 85px;">
                                    
                                    <div style="margin-bottom: 5px;">
                                        <select id=)HTML" \
R"HTML("my-pet-select" class="input" style="width: 85px; font-size: 9.5px; padding: 4px 6px;">
                                            <option value="0">妖怪1</option>
                                        </select>
                                    </div>
                                    
                                    
                                    <div style="display: flex; flex-direction: column; gap: 3px;">
                                        <div style="display: flex; align-items: center; padding: 3px 5px; background: var(--color-background-hover); border-radius: 2px; transition: all var(--duration-fast) var(--easing-standard);">
                                            <span style="font-size: 9px; color: var(--color-text-secondary); white-space: nowrap; flex-shrink: 0;">妖怪ID:</span>
                                            <span id="my-spirit-id" style="font-size: 9.5px; font-weight: 400; color: var(--color-text-primary); white-space: normal; word-wrap: br)HTML" \
R"HTML(eak-word; word-break: break-all; flex-shrink: 0; width: 40px; text-align: center;">0</span>
                                        </div>
                                        <div style="display: flex; align-items: center; padding: 3px 5px; background: var(--color-background-hover); border-radius: 2px; transition: all var(--duration-fast) var(--easing-standard);">
                                            <span style="font-size: 9px; color: var(--color-text-secondary); white-space: nowrap; flex-shrink: 0;">血量:</span>
                                            <span id="my-hp" style="font-size: 9.5px; font-weight: 400; color: var(--color-text-primary); white-space: normal; word-wrap: break-word; word-break: break-all; flex-shrink: 0; width: 40px; text-align: center;">0/0</span>
                                        </div>
                                        <div style="display: flex; align-items: center; padding: 3px 5px; background: var(--color-background-hover); border-ra)HTML" \
R"HTML(dius: 2px; transition: all var(--duration-fast) var(--easing-standard);">
                                            <span style="font-size: 9px; color: var(--color-text-secondary); white-space: nowrap; flex-shrink: 0;">妖怪名称:</span>
                                            <span id="my-name" style="font-size: 9.5px; font-weight: 400; color: var(--color-text-primary); white-space: normal; word-wrap: break-word; word-break: break-all; flex-shrink: 0; width: 40px; text-align: center;">未知</span>
                                        </div>
                                    </div>
                                </div>
                                
                                
                                <div style="width: 190px; height: 110px; flex: none; border: 1px solid var(--color-border); border-radius: 3px; overflow: hidden; background: var(--color-background); display: flex; flex-direction: column;">
                                    
                           )HTML" \
R"HTML(         <div style="display: grid; grid-template-columns: 105px 75px; background: var(--color-background-hover); padding: 3px 5px; border-bottom: 1px solid var(--color-border);">
                                        <div style="font-size: 9.5px; font-weight: 600; color: var(--color-text-primary); text-align: center;">技能名称</div>
                                        <div style="font-size: 9.5px; font-weight: 600; color: var(--color-text-primary); text-align: center;">PP值</div>
                                    </div>
                                    
                                    
                                    <div id="my-skill-list" style="overflow-y: auto; flex: 1; min-height: 0;">
                                        
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    
                    <div st)HTML" \
R"HTML(yle="width: 320px; height: 150px; opacity: 0; transform: translateY(4px); animation: fadeInUp var(--duration-slow) var(--easing-enter) forwards; animation-delay: 0.2s; flex: none;">
                        <div style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; box-shadow: var(--shadow-sm); height: 100%; display: flex; flex-direction: column;">
                            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px; min-height: 18px;">
                                <h3 style="font-size: 11.5px; font-weight: 600; color: var(--color-text-primary); margin: 0;">敌方数据</h3>
                                
                                <div id="other-buf-list" style="display: flex; flex-wrap: nowrap; gap: 2px; max-width: 200px; min-width: 0; overflow-x: hidden; overflow-y: hidden; padding-bottom: 4px;">
                                    
                              )HTML" \
R"HTML(  </div>
                            </div>
                            
                            
                            <div style="display: flex; gap: 8px; flex: 1;">
                                
                                <div style="flex: 0 0 85px;">
                                    
                                    <div style="margin-bottom: 5px;">
                                        <select id="other-pet-select" class="input" style="width: 85px; font-size: 9.5px; padding: 4px 6px;">
                                            <option value="0">敌方妖怪1</option>
                                        </select>
                                    </div>
                                    
                                    
                                    <div style="display: flex; flex-direction: column; gap: 3px;">
                                        <div style="display: flex; align-items: center; padding: 3px 5px; background: var(--color-backg)HTML" \
R"HTML(round-hover); border-radius: 2px; transition: all var(--duration-fast) var(--easing-standard);">
                                            <span style="font-size: 9px; color: var(--color-text-secondary); white-space: nowrap; flex-shrink: 0;">妖怪ID:</span>
                                            <span id="other-spirit-id" style="font-size: 9.5px; font-weight: 400; color: var(--color-text-primary); white-space: normal; word-wrap: break-word; word-break: break-all; flex-shrink: 0; width: 40px; text-align: center;">0</span>
                                        </div>
                                        <div style="display: flex; align-items: center; padding: 3px 5px; background: var(--color-background-hover); border-radius: 2px; transition: all var(--duration-fast) var(--easing-standard);">
                                            <span style="font-size: 9px; color: var(--color-text-secondary); white-space: nowrap; flex-shrink: 0;">血量:</span>
                                )HTML" \
R"HTML(            <span id="other-hp" style="font-size: 9.5px; font-weight: 400; color: var(--color-text-primary); white-space: normal; word-wrap: break-word; word-break: break-all; flex-shrink: 0; width: 40px; text-align: center;">0/0</span>
                                        </div>
                                        <div style="display: flex; align-items: center; padding: 3px 5px; background: var(--color-background-hover); border-radius: 2px; transition: all var(--duration-fast) var(--easing-standard);">
                                            <span style="font-size: 9px; color: var(--color-text-secondary); white-space: nowrap; flex-shrink: 0;">妖怪名称:</span>
                                            <span id="other-name" style="font-size: 9.5px; font-weight: 400; color: var(--color-text-primary); white-space: normal; word-wrap: break-word; word-break: break-all; flex-shrink: 0; width: 40px; text-align: center;">未知</span>
                                        </div>
       )HTML" \
R"HTML(                             </div>
                                </div>
                                
                                
                                <div style="width: 190px; height: 110px; flex: none; border: 1px solid var(--color-border); border-radius: 3px; overflow: hidden; background: var(--color-background); display: flex; flex-direction: column;">
                                    
                                    <div style="display: grid; grid-template-columns: 105px 75px; background: var(--color-background-hover); padding: 3px 5px; border-bottom: 1px solid var(--color-border);">
                                        <div style="font-size: 9.5px; font-weight: 600; color: var(--color-text-primary); text-align: center;">技能名称</div>
                                        <div style="font-size: 9.5px; font-weight: 600; color: var(--color-text-primary); text-align: center;">PP值</div>
                                    </div>
                         )HTML" \
R"HTML(           
                                    
                                    <div id="other-skill-list" style="overflow-y: auto; flex: 1; min-height: 0;">
                                        
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    
                    <div style="width: 230px; height: 150px; opacity: 0; transform: translateY(4px); animation: fadeInUp var(--duration-slow) var(--easing-enter) forwards; animation-delay: 0.3s; flex: none;">
                        <div style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; box-shadow: var(--shadow-sm); height: 100%; display: flex; flex-direction: column;">
                            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px; min-height:)HTML" \
R"HTML( 18px;">
                                <h3 style="font-size: 11.5px; font-weight: 600; color: var(--color-text-primary); margin: 0;">道具</h3>
                                <button onclick="window.refreshPackItems()" style="padding: 1px 4px; font-size: 8px; background: #2e7d32; color: white; border: none; border-radius: 2px; cursor: pointer;">刷新</button>
                            </div>
                            
                            
                            <div id="item-buttons-container" style="flex: 1; overflow-y: auto; overflow-x: hidden; margin-bottom: 4px; padding-right: 2px;">
                                <div style="display: grid; grid-template-columns: repeat(3, 1fr); gap: 2px;">
                                    <button class="item-btn item-btn-hp" data-id="100008" onclick="window.selectItem(100008, '小金创药', 20, 'hp')" style="display: flex; flex-direction: column; align-items: center; padding: 3px 4px; font-size: 8.5px; background: #fafafa; color: #333; )HTML" \
R"HTML(border: 1px solid #e0e0e0; border-radius: 3px; cursor: pointer; transition: all 0.15s ease;">
                                        <span class="item-name">小金创药</span>
                                        <span style="color: #e53935; font-size: 7.5px;">+20HP <span class="item-count" data-id="100008">(0)</span></span>
                                    </button>
                                    <button class="item-btn item-btn-hp" data-id="100007" onclick="window.selectItem(100007, '中金创药', 50, 'hp')" style="display: flex; flex-direction: column; align-items: center; padding: 3px 4px; font-size: 8.5px; background: #fafafa; color: #333; border: 1px solid #e0e0e0; border-radius: 3px; cursor: pointer; transition: all 0.15s ease;">
                                        <span class="item-name">中金创药</span>
                                        <span style="color: #e53935; font-size: 7.5px;">+50HP <span class="item-count" data-id="100007">(0)</span></span>
                         )HTML" \
R"HTML(           </button>
                                    <button class="item-btn item-btn-hp" data-id="100006" onclick="window.selectItem(100006, '大金创药', 100, 'hp')" style="display: flex; flex-direction: column; align-items: center; padding: 3px 4px; font-size: 8.5px; background: #fafafa; color: #333; border: 1px solid #e0e0e0; border-radius: 3px; cursor: pointer; transition: all 0.15s ease;">
                                        <span class="item-name">大金创药</span>
                                        <span style="color: #e53935; font-size: 7.5px;">+100HP <span class="item-count" data-id="100006">(0)</span></span>
                                    </button>
                                    <button class="item-btn item-btn-hp" data-id="100031" onclick="window.selectItem(100031, '活血散', 150, 'hp')" style="display: flex; flex-direction: column; align-items: center; padding: 3px 4px; font-size: 8.5px; background: #fafafa; color: #333; border: 1px solid #e0e0e0; border-radius: 3px)HTML" \
R"HTML(; cursor: pointer; transition: all 0.15s ease;">
                                        <span class="item-name">活血散</span>
                                        <span style="color: #e53935; font-size: 7.5px;">+150HP <span class="item-count" data-id="100031">(0)</span></span>
                                    </button>
                                    <button class="item-btn item-btn-mp" data-id="100010" onclick="window.selectItem(100010, '小型法力药剂', 5, 'mp')" style="display: flex; flex-direction: column; align-items: center; padding: 3px 4px; font-size: 8.5px; background: #fafafa; color: #333; border: 1px solid #e0e0e0; border-radius: 3px; cursor: pointer; transition: all 0.15s ease;">
                                        <span class="item-name">小型法力剂</span>
                                        <span style="color: #1976d2; font-size: 7.5px;">+5PP <span class="item-count" data-id="100010">(0)</span></span>
                                    </button>
                       )HTML" \
R"HTML(             <button class="item-btn item-btn-mp" data-id="100011" onclick="window.selectItem(100011, '中型法力药剂', 10, 'mp')" style="display: flex; flex-direction: column; align-items: center; padding: 3px 4px; font-size: 8.5px; background: #fafafa; color: #333; border: 1px solid #e0e0e0; border-radius: 3px; cursor: pointer; transition: all 0.15s ease;">
                                        <span class="item-name">中型法力剂</span>
                                        <span style="color: #1976d2; font-size: 7.5px;">+10PP <span class="item-count" data-id="100011">(0)</span></span>
                                    </button>
                                    <button class="item-btn item-btn-mp" data-id="100012" onclick="window.selectItem(100012, '大型法力药剂', 20, 'mp')" style="display: flex; flex-direction: column; align-items: center; padding: 3px 4px; font-size: 8.5px; background: #fafafa; color: #333; border: 1px solid #e0e0e0; border-radius: 3px; cursor: pointer; transition: all 0.15s )HTML" \
R"HTML(ease;">
                                        <span class="item-name">大型法力剂</span>
                                        <span style="color: #1976d2; font-size: 7.5px;">+20PP <span class="item-count" data-id="100012">(0)</span></span>
                                    </button>
                                </div>
                            </div>
                            
                            <div id="selected-item-hint" style="font-size: 8px; color: var(--color-text-tertiary); text-align: center; padding: 2px; background: var(--color-background-secondary); border-radius: 2px;">点击选择道具</div>
                            
                            
                            <div style="display: flex; gap: 4px;">
                                <button id="buy-item-btn" onclick="window.buySelectedItem()" style="flex: 1; padding: 4px 6px; font-size: 9px; background: #2e7d32; color: white; border: none; border-radius: 2px; cursor: pointer;">购买</button>
               )HTML" \
R"HTML(                 <button id="use-item-btn" onclick="window.useSelectedItem()" style="flex: 1; padding: 4px 6px; font-size: 9px; background: var(--color-primary); color: white; border: none; border-radius: 2px; cursor: pointer;">使用</button>
                            </div>
                            
                            
                            <div style="display: flex; align-items: center; gap: 4px; margin-top: 4px;">
                                <span style="font-size: 9px; color: var(--color-text-secondary);">数量:</span>
                                <input type="number" id="item-count-input" value="1" min="1" max="99" style="flex: 1; font-size: 9px; padding: 2px 4px; border: 1px solid var(--color-border); border-radius: 2px;">
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            
            <div id="monster" class="tab-content">
                <div style="display: flex)HTML" \
R"HTML(; gap: 10px; height: 100%;">
                    
                    <div class="panel-animate" style="flex: 1; background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 6px; box-shadow: var(--shadow-sm); display: flex; flex-direction: column;">
                        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
                            <div style="display: flex; align-items: center; gap: 6px;">
                                <h3 style="font-size: 11px; font-weight: 600; color: var(--color-text-primary); margin: 0;">妖怪列表</h3>
                                <div id="monster-count" class="badge badge-success" style="padding: 0 4px; font-size: 9px;">0</div>
                            </div>
                            <button onclick="window.queryMonsters()" style="padding: 2px 6px; font-size: 9px; background: #2e7d32; color: white; border: none; border-radius: 2px; cursor: poin)HTML" \
R"HTML(ter;">刷新妖怪</button>
                        </div>
                        
                        
                        <div style="flex: 1; border: 1px solid var(--color-border); border-radius: 3px; overflow: hidden; background: var(--color-background); display: flex; flex-direction: column;">
                            
                            <div style="display: grid; grid-template-columns: 28px 48px 28px 28px 26px 26px 24px 32px 130px 1fr; background: var(--color-background-hover); padding: 2px 4px; border-bottom: 1px solid var(--color-border); font-size: 9.5px; font-weight: 600; color: var(--color-text-primary);">
                                <div style="text-align: center;">#</div>
                                <div style="text-align: center;">名称</div>
                                <div style="text-align: center;">ID</div>
                                <div style="text-align: center;">性别</div>
                                <div style="text-align: center;">等级)HTML" \
R"HTML(</div>
                                <div style="text-align: center;">血量</div>
                                <div style="text-align: center;">系别</div>
                                <div style="text-align: center;">性格</div>
                                <div style="text-align: center;">资质</div>
                                <div style="text-align: left; padding-left: 4px;">技能</div>
                            </div>
                            
                            
                            <div id="monster-list" style="overflow-y: auto; flex: 1; font-size: 9.5px;">
                                <div style="text-align: center; padding: 20px; color: var(--color-text-tertiary);">暂无妖怪数据，请打开游戏妖怪背包</div>
                            </div>
                        </div>
                    </div>
                    
                    
                    <div class="panel-animate" style="width: 200px; background: var(--color-background); border: 1px solid var(--color-)HTML" \
R"HTML(border); border-radius: 4px; padding: 6px; box-shadow: var(--shadow-sm); display: flex; flex-direction: column;">
                        <h3 style="font-size: 11px; font-weight: 600; color: var(--color-text-primary); margin: 0 0 4px 0; border-bottom: 1px solid var(--color-border); padding-bottom: 4px;">妖怪详情</h3>
                        
                        <div id="monster-detail" style="flex: 1; overflow-y: auto; font-size: 9px;">
                            <div style="text-align: center; padding: 20px; color: var(--color-text-tertiary);">点击左侧妖怪查看详情</div>
                        </div>
                    </div>
                </div>
            </div>
            
            <div id="settings" class="tab-content">
                <div style="padding: 10px; display: flex; flex-direction: row; gap: 12px; align-items: flex-start; justify-content: center;">
                    
                    <div class="panel-animate" style="background: var(--color-background); border: 1px )HTML" \
R"HTML(solid var(--color-border); border-radius: 4px; padding: 12px; box-shadow: var(--shadow-sm); width: 180px; flex-shrink: 0;">
                        <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 10px;">
                            <span style="font-size: 13px; font-weight: 600; color: var(--color-text-primary);">变速</span>
                            <span id="speed-value" style="font-size: 12px; font-weight: 500; color: var(--color-primary); background: rgba(0, 120, 212, 0.1); padding: 3px 8px; border-radius: 10px;">1.0X</span>
                        </div>

                        <div style="position: relative; height: 24px; display: flex; align-items: center; justify-content: center;">
                            <input type="range" id="speed-slider" min="1" max="5" step="1" value="1"
                                style="width: 120px; cursor: pointer; accent-color: var(--color-primary); transition: all 0.2s ease;">
                 )HTML" \
R"HTML(       </div>

                        <div style="display: flex; justify-content: space-between; margin-top: 6px;">
                            <span style="font-size: 10px; color: var(--color-text-tertiary);">1X</span>
                            <span style="font-size: 10px; color: var(--color-text-tertiary);">5X</span>
                        </div>
                    </div>

                    
                    <div class="panel-animate" style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 12px; box-shadow: var(--shadow-sm); width: 140px; flex-shrink: 0; height: 99px; display: flex; flex-direction: column;">
                        <div style="font-size: 13px; font-weight: 600; color: var(--color-text-primary); margin-bottom: 8px;">游戏辅助</div>

                        <div style="display: flex; flex-direction: column; gap: 2px; justify-content: center; flex: 1;">
                            <label style="display: flex;)HTML" \
R"HTML( align-items: center; gap: 6px; cursor: pointer; font-size: 10px; color: var(--color-text-primary); padding: 2px 0;">
                                <input type="checkbox" id="auto-heal" style="width: 12px; height: 12px; cursor: pointer;" onchange="toggleAutoHeal(this.checked)">
                                <span>自动回血</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 6px; cursor: pointer; font-size: 10px; color: var(--color-text-primary); padding: 2px 0;">
                                <input type="checkbox" id="battlesix-auto-battle" style="width: 12px; height: 12px; cursor: pointer;" onchange="toggleBattleSixAutoBattle(this.checked)">
                                <span>自动战斗</span>
                            </label>
                        </div>
                    </div>
                </div>
            </div>
            
            <div id="helper-log" class="tab-content">
                <d)HTML" \
R"HTML(iv style="display: flex; flex-direction: column; height: 100%; gap: 6px; padding: 6px 12px 6px 12px;">
                    
                    <div style="display: flex; justify-content: space-between; align-items: center; min-height: 16px;">
                        <span id="helper-log-count" style="font-size: 10px; color: var(--color-text-secondary);">总记录数: 0</span>
                        <button onclick="window.clearHelperLog()" style="padding: 2px 8px; font-size: 10px; background: var(--color-background); color: var(--color-text-primary); border: 1px solid var(--color-border); border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">清空日志</button>
                    </div>

                    
                    <div style="flex: 1; border: 1px solid var(--color-border); border-radius: 3px; overflow: hidden; background: var(--color-background); display: flex; flex-direction: column; min-height: 100px;">
                    )HTML" \
R"HTML(    
                        <div style="display: grid; grid-template-columns: 60px 90px 1fr; background: var(--color-background-hover); padding: 3px 8px; border-bottom: 1px solid var(--color-border); font-size: 10px; font-weight: 600; color: var(--color-text-primary); flex-shrink: 0;">
                            <div style="text-align: center;">序号</div>
                            <div style="text-align: center;">时间</div>
                            <div style="text-align: center;">内容</div>
                        </div>

                        
                        <div id="helper-log-list" style="overflow-y: auto; flex: 1; font-size: 10.5px; min-height: 90px;">
                            <div style="text-align: center; padding: 30px; color: var(--color-text-tertiary);">暂无日志记录</div>
                        </div>
                    </div>
                </div>
            </div>
            
            <div id="daily" class="tab-content">
                
                <di)HTML" \
R"HTML(v class="sub-tabs">
                    <button class="sub-tab-button active" onclick="openSubTab(event, 'sub-daily')">日常活动</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-taskzone')">任务区</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-latest')">最新活动</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-materials')">材料采集</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-boss')">BOSS专区</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-lingyu')">灵玉区</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-dungeon')">副本区</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-battlesix')" style="display: none;">万妖盛会</button>
                    <button class="sub-tab-button" onclick="openSubTab(event, 'sub-spirit)HTML" \
R"HTML(collect')">精魄系统</button>
                </div>

                
                <div id="sub-daily" class="sub-tab-content active">
                    <div class="fx-page">
                        
                        <div class="fx-row">

                            
                            <div class="fx-panel" style="flex: 2.2;">
                                <span class="fx-panel-title">每日</span>
                                <div class="fx-checks" style="grid-template-columns: repeat(4, minmax(0, 1fr));">
                                    <label class="fx-check">
                                        <input type="checkbox" id="daily-deep-dig" checked style="width: 11px; height: 11px;">
                                        <span>深度挖宝</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="family-checkin" checked style="width: 11px; height)HTML" \
R"HTML(: 11px;">
                                        <span>家族考勤</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="daily-card" checked style="width: 11px; height: 11px;">
                                        <span>每日卡牌</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="family-report" checked style="width: 11px; height: 11px;">
                                        <span>家族报道</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="daily-gift" checked style="width: 11px; height: 11px;">
                                        <span>每日礼包</span>
                                    </label>
                       )HTML" \
R"HTML(             <label class="fx-check">
                                        <input type="checkbox" id="family-defend" checked style="width: 11px; height: 11px;">
                                        <span>家族保卫</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="weekly-gift" checked style="width: 11px; height: 11px;">
                                        <span>每周礼包</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="shop-surprise" checked style="width: 11px; height: 11px;">
                                        <span>商城惊喜</span>
                                    </label>
                                </div>
                                <div class="fx-actions">
                                    <button id="complete-da)HTML" \
R"HTML(ily-tasks" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 8px; width: 100%;">一键完成</button>
                                </div>
                            </div>

                            
                            <div class="fx-panel">
                                <span class="fx-panel-title">试炼</span>
                                <div class="fx-checks" style="grid-template-columns: 1fr;">
                                    <label class="fx-check">
                                        <input type="checkbox" id="daily-fire-wind" style="width: 11px; height: 11px;">
                                        <span>火风试炼</span>
                                    </label>
                                    <label class="fx-check">
                                        <input type="checkbox" id="daily-fire" style="width: 11px; height: 11px;">
                                        <span>火焰试炼</span>
                                    </label>
             )HTML" \
R"HTML(                       <label class="fx-check">
                                        <input type="checkbox" id="daily-storm" style="width: 11px; height: 11px;">
                                        <span>风暴试炼</span>
                                    </label>
                                </div>
                                <div class="fx-actions">
                                    <button id="complete-trial-tasks" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 8px; width: 100%;">一键试炼</button>
                                </div>
                            </div>

                            
                            <div class="fx-panel">
                                <span class="fx-panel-title">跳舞大赛</span>
                                <label class="fx-check">
                                    <input type="checkbox" id="daily-dance" style="width: 11px; height: 11px;">
                                    <span>自动跳舞</span>
                     )HTML" \
R"HTML(           </label>
                                <div style="font-size: 9px; color: var(--color-text-secondary); flex: 0 0 auto;">已完成: <span id="dance-count" style="color: var(--color-primary); font-weight: 600;">0</span>/3 次</div>
                                <div class="fx-actions">
                                    <button id="complete-dance-tasks" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 6px; width: 100%;">一键跳舞</button>
                                </div>
                            </div>

                            
                            <div class="fx-panel" style="flex: 1.3;">
                                <span class="fx-panel-title">玄塔</span>
                                <div class="fx-actions" style="margin-top: 0;">
                                    <button id="one-key-xuantta" class="btn btn-primary" style="flex: 1; font-size: 9.5px; padding: 3px 4px;">一键玄塔</button>
                                    <button id="buy-dice-18" c)HTML" \
R"HTML(lass="btn btn-secondary" style="flex: 1; font-size: 9.5px; padding: 3px 4px;">购买骰子18</button>
                                </div>
                                <label class="fx-check" style="flex: 0 0 auto;">
                                    <input type="checkbox" id="auto-go-home" style="width: 12px; height: 12px; cursor: pointer;" onchange="toggleAutoGoHome(this.checked)">
                                    <span>自动回家</span>
                                </label>
                                <label class="fx-check" style="flex: 0 0 auto;">
                                    <input type="checkbox" id="block-battle" style="width: 12px; height: 12px; cursor: pointer;" onchange="toggleBlockBattle(this.checked)">
                                    <span>屏蔽战斗</span>
                                </label>
                            </div>

                            
                            <div class="fx-panel" style="flex: 1.2;">
                                <sp)HTML" \
R"HTML(an class="fx-panel-title">福瑞宝箱</span>
                                <div class="fx-panel-note">自动福瑞 11:00-13:00 / 17:00-19:00</div>
                                <div style="display: flex; align-items: center; gap: 4px; flex: 0 0 auto;">
                                    <label style="font-size: 9.5px; color: var(--color-text-secondary);">目标:</label>
                                    <input type="number" id="heaven-furui-max" value="30" min="1" max="30" style="width: 40px; font-size: 9.5px; padding: 2px 4px; border: 1px solid var(--color-border); border-radius: 2px;">
                                    <span style="font-size: 9.5px; color: var(--color-text-secondary);">个</span>
                                </div>
                                <div style="font-size: 9.5px; color: var(--color-text-secondary); flex: 0 0 auto;">已拾取: <span id="heaven-furui-count" style="color: var(--color-primary); font-weight: 600;">0</span>/<span id="heaven-furui-target">30</span></div>
    )HTML" \
R"HTML(                            <div class="fx-actions">
                                    <button id="start-heaven-furui" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 6px; flex: 1;">开始</button>
                                    <button id="stop-heaven-furui" class="btn btn-secondary" style="font-size: 9.5px; padding: 3px 6px; flex: 1;" disabled>停止</button>
                                </div>
                            </div>

                            
                            <div class="fx-panel" style="flex: 1.3;">
                                <span class="fx-panel-title">坐骑大赛</span>
                                <div class="fx-panel-note">多人竞速 · 每日50点上限 · 固定临时坐骑</div>
                                <div style="font-size: 9.5px; color: var(--color-text-secondary); flex: 0 0 auto;">可获得积分: <span id="horse-competition-points" style="color: var(--color-primary); font-weight: 600;">--</span></div>
                                <div style="font-size: 9.5)HTML" \
R"HTML(px; color: var(--color-text-secondary); flex: 0 0 auto;">状态: <span id="horse-competition-status" style="color: var(--color-text-tertiary);">就绪</span></div>
                                <div class="fx-actions">
                                    <button id="one-key-horse-competition" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 6px; flex: 1;">开始</button>
                                    <button id="stop-horse-competition" class="btn" style="font-size: 9.5px; padding: 3px 6px; flex: 1; background: #fff4e5; color: #9a5b00; border: 1px solid #ffd08a;">停止</button>
                                </div>
                            </div>

                        </div>
                        
                        <div id="daily-task-status" style="font-size: 9px; color: var(--color-text-secondary); padding: 2px 6px; background: var(--color-background-secondary); border-radius: 2px; text-align: center; flex: 0 0 auto;">
                            勾选需要完成的日常活动，点击对应按)HTML" \
R"HTML(钮执行
                        </div>
                    </div>
                </div>

                <div id="sub-taskzone" class="sub-tab-content">
                    <div class="fx-row" style="padding: 4px 6px; flex-wrap: wrap;">

                        
                        <div class="fx-panel" style="flex: 0 0 240px;">
                            <span class="fx-panel-title">八卦灵盘</span>
                            <div class="fx-panel-note" style="white-space: normal;">按任务链顺序推进，覆盖对话、采集与场景切换，最终战斗自动处理。</div>
                            <div class="fx-actions">
                                <button id="start-task-zone" class="btn btn-primary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;">开始</button>
                                <button id="stop-task-zone" class="btn btn-secondary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;" disabled>停止</button>
                            </div>
                            <div id="task-zone-status" style="font-size: 9px; c)HTML" \
R"HTML(olor: var(--color-text-secondary); background: var(--color-background-secondary); border-radius: 2px; padding: 2px 6px; flex: 0 0 auto;">等待开始</div>
                        </div>

                            
                            <div id="huangcheng-weiji-panel" hidden style="flex: 1 1 560px; min-width: 560px; background: #FFFFFF; border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; display: none; flex-direction: column; gap: 5px;">
                                <div style="display: flex; justify-content: space-between; align-items: center; gap: 8px;">
                                    <div style="font-size: 11px; font-weight: 600; color: var(--color-text-primary);">皇城危机</div>
                                    <label style="display: flex; align-items: center; gap: 4px; font-size: 9.5px; color: var(--color-text-secondary); cursor: pointer; user-select: none;">
                                        <input type="checkbox" id="huangcheng-hide-completed" c)HTML" \
R"HTML(hecked style="width: 12px; height: 12px; cursor: pointer;">
                                        <span>隐藏已完成</span>
                                    </label>
                                </div>
                                <div style="font-size: 9px; color: var(--color-text-secondary); line-height: 1.4;">按子任务 ID 顺序批处理，已完成任务默认不显示；任务完成状态来自运行时进度回包。</div>
                                <div style="display: flex; flex-wrap: wrap; gap: 4px;">
                                    <button id="refresh-huangcheng-weiji" class="btn btn-secondary" style="font-size: 9.5px; padding: 3px 8px;">刷新进度</button>
                                    <button id="select-all-huangcheng-weiji" class="btn btn-secondary" style="font-size: 9.5px; padding: 3px 8px;">全选未完成</button>
                                    <button id="clear-huangcheng-weiji" class="btn btn-secondary" style="font-size: 9.5px; padding: 3px 8px;">清空选择</button>
                                    <button id="start-huangcheng-weiji)HTML" \
R"HTML(" class="btn btn-primary" style="font-size: 9.5px; padding: 3px 8px;">完成所选</button>
                                    <button id="stop-huangcheng-weiji" class="btn btn-secondary" style="font-size: 9.5px; padding: 3px 8px;" disabled>停止</button>
                                </div>
                                <div id="huangcheng-weiji-summary" style="font-size: 9.5px; color: var(--color-text-secondary); background: var(--color-background-secondary); border-radius: 2px; padding: 3px 6px;">已完成: 0 / 8，已勾选: 0</div>
                                <div style="border: 1px solid var(--color-border); border-radius: 3px; overflow: hidden; background: var(--color-background); display: flex; flex-direction: column;">
                                    <div style="display: grid; grid-template-columns: 30px 1.2fr 64px 120px 90px; background: var(--color-background-hover); padding: 3px 6px; border-bottom: 1px solid var(--color-border); font-size: 10px; font-weight: 600; color: var(--color-tex)HTML" \
R"HTML(t-primary);">
                                        <div style="text-align: center;">选</div>
                                        <div style="text-align: center;">子任务</div>
                                        <div style="text-align: center;">场景</div>
                                        <div style="text-align: center;">奖励</div>
                                        <div style="text-align: center;">状态</div>
                                    </div>
                                    <div id="huangcheng-weiji-list" style="overflow-y: auto; flex: 1 1 auto; min-height: 0; font-size: 9.5px;">
                                        <div style="text-align: center; padding: 12px; color: var(--color-text-tertiary);">正在加载皇城危机列表...</div>
                                    </div>
                                </div>
                                <div id="huangcheng-weiji-status" style="font-size: 9px; color: var(--color-text-secondary); background: var(--color-background-seco)HTML" \
R"HTML(ndary); border-radius: 2px; padding: 3px 6px;">等待刷新进度</div>
                            </div>
                    </div>
                </div>

                <div id="sub-latest" class="sub-tab-content">
                    <div class="fx-panel" style="margin: 4px 6px; align-self: stretch;">
                        
                        <div style="display: flex; align-items: center; justify-content: space-between; gap: 8px; flex: 0 0 auto;">
                            <div style="font-size: 11px; font-weight: 600; color: var(--color-text-primary);">活动选择</div>
                            <div id="act-latest-hint" style="font-size: 9px; color: var(--color-text-tertiary);">勾选要执行的活动，点击“一键执行”；扫荡自动判定</div>
                        </div>

                        
                        <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(232px, 1fr)); gap: 2px 6px; flex: 0 0 auto;">
                            <label class="act-item" id="act-row-647">
         )HTML" \
R"HTML(                       <input type="checkbox" id="act-check-647" class="act-pick">
                                <span class="act-item-name">突破迷阵</span>
                                <span class="act-item-count" id="act-count-647">--</span>
                            </label>
                            <label class="act-item" id="act-row-869">
                                <input type="checkbox" id="act-check-869" class="act-pick">
                                <span class="act-item-name">摘星之旅</span>
                                <span class="act-item-count" id="act-count-869">--</span>
                            </label>
                            <label class="act-item" id="act-row-749">
                                <input type="checkbox" id="act-check-749" class="act-pick">
                                <span class="act-item-name">寒冰洞历险记</span>
                                <span class="act-item-count" id="act-count-749">--</span>
                            </l)HTML" \
R"HTML(abel>
                            <label class="act-item" id="act-row-870">
                                <input type="checkbox" id="act-check-870" class="act-pick">
                                <span class="act-item-name">月饼大胃王</span>
                                <span class="act-item-count" id="act-count-870">--</span>
                            </label>
                            <label class="act-item" id="act-row-704">
                                <input type="checkbox" id="act-check-704" class="act-pick">
                                <span class="act-item-name">夺回花束行动</span>
                                <span class="act-item-count" id="act-count-704">--</span>
                            </label>
                            <label class="act-item" id="act-row-697">
                                <input type="checkbox" id="act-check-697" class="act-pick">
                                <span class="act-item-name">美食大作战</span>
                                <s)HTML" \
R"HTML(pan class="act-item-count" id="act-count-697">--</span>
                            </label>
                        </div>

                        
                        <div style="display: flex; align-items: center; gap: 6px; margin-top: 4px; flex: 0 0 auto;">
                            <button onclick="selectAllActivities(true)" class="btn btn-secondary" style="font-size: 10px; padding: 3px 10px;">全选</button>
                            <button onclick="selectAllActivities(false)" class="btn btn-secondary" style="font-size: 10px; padding: 3px 10px;">全不选</button>
                            <button onclick="refreshActivityCounts()" class="btn btn-secondary" style="font-size: 10px; padding: 3px 10px;">刷新次数</button>
                            <button onclick="runSelectedActivities()" class="btn btn-primary" style="font-size: 10.5px; padding: 3px 14px; margin-left: auto;">一键执行所选</button>
                        </div>
                    </div>
                </div>

            )HTML" \
R"HTML(    <div id="sub-materials" class="sub-tab-content">
                    <div class="fx-panel" style="margin: 4px 6px; flex: 0 1 auto; align-self: stretch;">
                        
                        <div style="font-size: 11px; font-weight: 600; color: var(--color-text-primary); flex: 0 0 auto;">每日采集</div>

                        
                        <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(112px, 1fr)); gap: 2px 4px; flex: 0 0 auto;">
                            
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-0" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>上清宝玉</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: p)HTML" \
R"HTML(ointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-1" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>天地灵气</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-2" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>火云岩</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-3" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                        )HTML" \
R"HTML(        <span>千年红木</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-4" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>远古青松</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-5" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>精蓝石</span>
                            </label>
                            
                            
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; f)HTML" \
R"HTML(ont-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-6" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>松绿石</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-7" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>冰霜岩</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-8" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <s)HTML" \
R"HTML(pan>幻影石</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-9" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>冰晶砂</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-10" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>天罡石</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <)HTML" \
R"HTML(input type="checkbox" id="collect-11" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>茅山玉</span>
                            </label>
                            
                            
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-12" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>神明果</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-13" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>朱砂石<)HTML" \
R"HTML(/span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-14" checked style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>青金石</span>
                            </label>
                            <label style="display: flex; align-items: center; gap: 3px; cursor: pointer; font-size: 10.5px; color: #333333;">
                                <input type="checkbox" id="collect-15" style="width: 12px; height: 12px; border: 1px solid #CCCCCC; cursor: pointer;">
                                <span>坐骑蛋</span>
                            </label>
                        </div>
                        
                        
                        <div style="display: flex; justify-content: flex-end; gap: 6px; margin-top: 4px; flex)HTML" \
R"HTML(: 0 0 auto;">
                            <button onclick="selectAllCollect(true)" style="background: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; border-radius: 3px; padding: 3px 10px; font-size: 10px; font-weight: 500; cursor: pointer; transition: all 0.2s;" onmouseover="this.style.background='#F5F5F5'" onmouseout="this.style.background='#FFFFFF'">全选</button>
                            <button onclick="selectAllCollect(false)" style="background: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; border-radius: 3px; padding: 3px 10px; font-size: 10px; font-weight: 500; cursor: pointer; transition: all 0.2s;" onmouseover="this.style.background='#F5F5F5'" onmouseout="this.style.background='#FFFFFF'">全不选</button>
                            <button onclick="startOneKeyCollect()" style="background: var(--color-primary); color: #FFFFFF; border: none; border-radius: 3px; padding: 3px 12px; font-size: 10.5px; font-weight: 600; cursor: pointer; transition: background 0.2s;" onmouseov)HTML" \
R"HTML(er="this.style.background='var(--color-primary-hover)'" onmouseout="this.style.background='var(--color-primary)'" onmousedown="this.style.background='var(--color-primary-pressed)'" onmouseup="this.style.background='var(--color-primary-hover)'">一键采集</button>
                        </div>
                    </div>
                </div>

                <div id="sub-boss" class="sub-tab-content">
                    
                    <div style="display: flex; flex-direction: column; gap: 8px; padding: 8px;">
                        <div class="panel-animate" style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 10px; box-shadow: var(--shadow-sm); width: 100%; max-width: 340px; min-width: 280px;">
                            <div style="font-size: 11.5px; font-weight: 600; color: var(--color-text-primary); margin-bottom: 8px;">boss对战</div>
                            
                            
                            <)HTML" \
R"HTML(div style="display: flex; gap: 6px; align-items: center; min-width: 0;">
                                
                                <div style="display: flex; gap: 4px; flex: 1; min-width: 0;">
                                    <input type="text" id="boss-search-input" placeholder="搜索..."
                                        style="width: 72px; padding: 5px 8px; font-size: 10.5px; border: 1px solid var(--color-border); border-radius: var(--radius-sm); outline: none; transition: all var(--duration-normal) var(--easing-standard); background: var(--color-background); color: var(--color-text-primary);"
                                        oninput="filterBossSelect()">
                                    <select id="boss-select"
                                        style="flex: 1; min-width: 0; padding: 5px 10px; font-size: 10.5px; border: 1px solid var(--color-border); border-radius: var(--radius-sm); outline: none; transition: all var(--duration-normal) var(--easing-stand)HTML" \
R"HTML(ard); background: var(--color-background); color: var(--color-text-primary); cursor: pointer;"
                                        onchange="selectBoss(this.value)">
                                        <option value="0">-- 请选择BOSS --</option>
                                        
                                    </select>
                                </div>
                                
                                
                                <button id="enter-boss-battle-btn" onclick="enterBossBattle()" 
                                    style="flex: 0 0 auto; padding: 4px 10px; font-size: 10px; background: var(--color-primary); color: white; border: none; border-radius: 2px; cursor: pointer; transition: all 0.2s linear; white-space: nowrap;"
                                    onmouseover="this.style.background='var(--color-primary-hover)'" 
                                    onmouseout="this.style.background='var(--color-primary)'">
                   )HTML" \
R"HTML(                 进入战斗
                                </button>
                            </div>
                            
                            
                            <div id="selected-boss-info" style="margin-top: 8px; font-size: 9.5px; color: var(--color-text-secondary);">
                                请选择一个BOSS
                            </div>
                        </div>
                    </div>
                </div>

                <div id="sub-lingyu" class="sub-tab-content">
                    <div class="fx-row">
                        
                        <div class="fx-panel" style="flex: 1.15;">
                            <div style="display: flex; justify-content: space-between; align-items: center; gap: 6px; flex: 0 0 auto;">
                                <div style="display: flex; align-items: center; gap: 5px; min-width: 0;">
                                    <span class="fx-panel-title">灵玉列表</span>
                                 )HTML" \
R"HTML(   <div id="lingyu-count" class="badge badge-success" style="padding: 0 6px; font-size: 10px;">0</div>
                                </div>
                                
                                <div style="display: flex; align-items: center; gap: 4px; flex: 0 0 auto;">
                                    <select id="lingyu-sort-attr" style="font-size: 10px; padding: 1px 4px; border: 1px solid var(--color-border); border-radius: 2px; outline: none; height: 19px; background: var(--color-background);">
                                        <option value="none">不排序</option>
                                        <option value="体力">体力</option>
                                        <option value="攻击">攻击</option>
                                        <option value="防御">防御</option>
                                        <option value="法术">法术</option>
                                        <option value="抗性">抗性</option>
                                        <option value)HTML" \
R"HTML(="速度">速度</option>
                                        <option value="威力">威力</option>
                                    </select>
                                    <button onclick="window.applyLingyuSort()" style="padding: 1px 8px; font-size: 10px; background: var(--color-primary); color: white; border: none; border-radius: 2px; cursor: pointer; height: 19px;">排序</button>
                                    <button onclick="window.queryLingyu()" style="padding: 1px 8px; font-size: 10px; background: #2e7d32; color: white; border: none; border-radius: 2px; cursor: pointer; height: 19px;">查询灵玉</button>
                                </div>
                            </div>

                            
                            <div style="flex: 1 1 auto; min-height: 0; border: 1px solid var(--color-border); border-radius: 3px; overflow: hidden; background: var(--color-background); display: flex; flex-direction: column;">
                                <div style="display: gri)HTML" \
R"HTML(d; grid-template-columns: 55px 40px 65px 1fr; background: var(--color-background-hover); padding: 2px 6px; border-bottom: 1px solid var(--color-border); font-size: 10px; font-weight: 600; color: var(--color-text-primary); flex: 0 0 auto;">
                                    <div style="text-align: center;">名称</div>
                                    <div style="text-align: center;">索引</div>
                                    <div style="text-align: center;">宠物</div>
                                    <div style="text-align: center;">附加属性</div>
                                </div>
                                <div id="lingyu-list" class="fx-scroll" style="font-size: 10px;">
                                    <div style="text-align: center; padding: 10px; color: var(--color-text-tertiary);">暂无数据</div>
                                </div>
                            </div>
                        </div>

                        
                        <div class="fx-panel" st)HTML" \
R"HTML(yle="flex: 0.85;">
                            <span class="fx-panel-title" style="border-bottom: 1px solid var(--color-border); padding-bottom: 3px;">分解队列</span>

                            
                            <div style="flex: 1 1 auto; min-height: 0; border: 1px solid var(--color-border); border-radius: 3px; overflow: hidden; background: var(--color-background); display: flex; flex-direction: column;">
                                <div style="display: grid; grid-template-columns: 45px 1fr; background: var(--color-background-hover); padding: 2px 6px; border-bottom: 1px solid var(--color-border); font-size: 10px; font-weight: 600; color: var(--color-text-primary); flex: 0 0 auto;">
                                    <div style="text-align: center;">#</div>
                                    <div style="text-align: center;">灵玉名称</div>
                                </div>
                                <div id="decompose-list" class="fx-scroll" style="font-size: 10px;")HTML" \
R"HTML(>
                                    <div style="text-align: center; padding: 10px; color: var(--color-text-tertiary);">未添加灵玉</div>
                                </div>
                            </div>

                            
                            <div class="fx-actions" style="display: grid; grid-template-columns: 1fr 1fr;">
                                <button onclick="window.decomposeAllInQueue()" class="btn btn-primary" style="font-size: 10.5px; height: 21px; padding: 0; line-height: 21px;">一键分解</button>
                                <button onclick="window.clearDecomposeQueue()" class="btn btn-secondary" style="font-size: 10.5px; height: 21px; padding: 0; line-height: 21px;">清空队列</button>
                            </div>

                            
                            <div style="font-size: 9.5px; color: var(--color-text-secondary); line-height: 1.3; flex: 0 0 auto;">
                                <span id="decompose-queue-count">已添加: 0 个灵玉</spa)HTML" \
R"HTML(n>
                                <span style="color: var(--color-text-tertiary);"> · 分解后无法找回</span>
                            </div>
                        </div>
                    </div>
                </div>

            
            <div id="sub-dungeon" class="sub-tab-content">
                <div class="fx-row" style="padding: 4px 6px;">

                    
                    <div class="fx-panel" style="flex: 1;">
                        <span class="fx-panel-title">副本跳层</span>

                        
                        <div style="display: flex; align-items: center; gap: 4px; flex: 0 0 auto;">
                            <label for="dungeon-layer-input" style="font-size: 9.5px; color: var(--color-text-secondary);">目标层数:</label>
                            <input type="number" id="dungeon-layer-input" value="1" min="1" max="9999"
                                style="width: 50px; font-size: 9.5px; padding: 2px 4px; border: 1px solid var(--color-border); border)HTML" \
R"HTML(-radius: 2px;">
                            <span style="font-size: 8.5px; color: var(--color-text-tertiary);">层</span>
                        </div>

                        <div class="fx-panel-note">需50级以下妖怪</div>

                        <div class="fx-actions">
                            <button id="dungeon-jump-btn" onclick="startDungeonJump()" class="btn btn-primary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;">跳层</button>
                            <button id="dungeon-stop-btn" onclick="stopDungeonJump()" class="btn btn-secondary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;">停止</button>
                        </div>
                    </div>

                    
                    <div class="fx-panel" style="flex: 1.35;">
                        <span class="fx-panel-title">双台谷刷级</span>

                        <div style="display: flex; align-items: center; gap: 4px; flex: 0 0 auto;">
                            <label style="font-size: 9.5px; color: va)HTML" \
R"HTML(r(--color-text-secondary); min-width: 35px;">主宠:</label>
                            <select id="shuangtai-pet" style="flex: 1; min-width: 0; font-size: 9.5px; padding: 2px; border: 1px solid var(--color-border); border-radius: 2px; height: 18px;" disabled>
                                <option value="">请先查询</option>
                            </select>
                            <button id="query-shuangtai" class="btn btn-secondary" style="flex: 0 0 auto; font-size: 9.5px; padding: 2px 6px;">查询</button>
                        </div>

                        <div style="display: flex; align-items: center; gap: 4px; flex: 0 0 auto;">
                            <label style="font-size: 9.5px; color: var(--color-text-secondary); min-width: 35px;">技能:</label>
                            <select id="shuangtai-skill" style="flex: 1; min-width: 0; font-size: 9.5px; padding: 2px; border: 1px solid var(--color-border); border-radius: 2px; height: 18px;" disabled>
                         )HTML" \
R"HTML(       <option value="">自动选择最高威力</option>
                            </select>
                        </div>

                        <div style="display: flex; align-items: center; gap: 6px; flex: 0 0 auto;">
                            <label class="fx-check" style="flex: 0 0 auto;">
                                <input type="checkbox" id="shuangtai-block-battle" style="width: 11px; height: 11px;">
                                <span>屏蔽战斗</span>
                            </label>
                            <span class="fx-panel-note">主宠末位 · 至少2只妖怪</span>
                        </div>

                        <div class="fx-actions">
                            <button id="start-shuangtai" class="btn btn-primary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;" disabled>启动</button>
                            <button id="stop-shuangtai" class="btn btn-secondary" style="flex: 1; font-size: 9.5px; padding: 3px 6px;" disabled>停止</button>
                        </div>
     )HTML" \
R"HTML(               </div>

                </div>
            </div>

            
            <div id="sub-battlesix" class="sub-tab-content" style="display: none;">
                <div style="display: flex; gap: 12px; height: 100%; padding: 4px;">
                    
                    <div style="display: flex; flex-direction: column; gap: 6px; min-width: 160px;">
                        <div style="font-size: 11px; font-weight: 600; color: var(--color-text-primary);">万妖盛会PVP</div>
                        
                        
                        <div style="display: flex; align-items: center; gap: 6px;">
                            <label for="battlesix-match-count" style="font-size: 10.5px; color: var(--color-text-primary); min-width: 70px;">匹配次数：</label>
                            <input type="number" id="battlesix-match-count" value="1" min="1" max="999" class="input" style="width: 60px; font-size: 10.5px; padding: 3px 5px;">
                        </div>

             )HTML" \
R"HTML(           
                        <div style="display: flex; gap: 6px;">
                            <button id="battlesix-auto-match" class="btn btn-primary" style="font-size: 10.5px; padding: 4px 8px;">自动匹配</button>
                            <button id="battlesix-cancel-match" class="btn btn-secondary" style="font-size: 10.5px; padding: 4px 8px;">取消匹配</button>
                        </div>
                    </div>
                    
                    
                    <div style="width: 1px; background: var(--color-border);"></div>
                    
                    
                    <div style="flex: 1;">
                        <div style="font-size: 10.5px; font-weight: 600; color: var(--color-text-secondary); margin-bottom: 4px;">功能说明</div>
                        <div style="font-size: 9.5px; color: var(--color-text-tertiary); line-height: 1.5;">
                            <div>• 自动匹配后进入战斗</div>
                            <div>• 自动战斗会自动选择技能</div>
       )HTML" \
R"HTML(                     <div>• 精灵死亡时自动切换</div>
                            <div>• 战斗结束后自动确认</div>
                            <div>• 支持多次自动匹配循环</div>
                        </div>
                    </div>
                </div>
            </div>

            
            <div id="sub-spiritcollect" class="sub-tab-content">
                <div class="spirit-collect-layout">
                    
                    <div class="spirit-toolbar">
                        <span class="spirit-stat-chip">今日可赠 <span id="spirit-daily-out" style="color: var(--color-primary); font-weight: 600;">-</span></span>
                        <span class="spirit-stat-chip">本周可赠 <span id="spirit-weekly-out" style="color: var(--color-primary); font-weight: 600;">-</span></span>
                        <div class="spirit-toolbar-selected">已选精魄: <span id="spirit-selected" style="color: var(--color-primary); font-weight: 600;">无</span></div>
                        <div class="spirit-toolbar-send">
           )HTML" \
R"HTML(                 <label>卡布号:</label>
                            <input type="text" id="spirit-friend-id" placeholder="输入对方卡布号">
                            <button onclick="verifyAndSendSpirit()" class="btn btn-primary">赠送</button>
                        </div>
                        <div class="spirit-status-line" id="spirit-status"></div>
                        <button onclick="refreshSpiritPanel()" class="btn btn-secondary">刷新</button>
                    </div>

                    
                    <div class="spirit-columns">
                        
                        <div class="spirit-giving-panel">
                            <div id="spirit-list">
                                <div style="color: var(--color-text-tertiary); text-align: center; padding: 10px 8px;">点击“刷新”加载精魄列表</div>
                            </div>
                        </div>

                        
                        <div class="spirit-record-side">
                            <div c)HTML" \
R"HTML(lass="spirit-record-header">
                                <div class="spirit-record-title">记录面板</div>
                                <div class="spirit-record-meta">
                                    <div id="spirit-record-title" class="spirit-record-subtitle">等待加载</div>
                                    <div class="spirit-giving-actions">
                                        <button onclick="getSpiritRecord(1)" class="btn btn-secondary">赠送</button>
                                        <button onclick="getSpiritRecord(2)" class="btn btn-secondary">接收</button>
                                    </div>
                                </div>
                            </div>

                            <div id="spirit-record-panel" class="spirit-record-body">
                                点击上方按钮查看赠送记录或接收记录。
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            </div>

          )HTML" \
R"HTML(  
            <div id="wpe" class="tab-content">
                
                <div class="panel-animate" style="display: flex; gap: 10px; margin-bottom: 10px; flex-wrap: wrap; align-items: stretch;">
                    
                    <div style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; width: 120px; box-shadow: var(--shadow-sm); display: flex; flex-direction: column;">
                        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
                            <h3 style="font-size: 12px; font-weight: 600; color: var(--color-text-primary); margin: 0;">拦截类型</h3>
                        </div>
                        <div style="display: flex; flex-direction: column; gap: 4px;">
                            <div style="display: flex; align-items: center; gap: 5px;">
                                <input type="checkbox" id="intercept-send" checked st)HTML" \
R"HTML(yle="width: 12px; height: 12px;">
                                <label for="intercept-send" style="font-size: 10px; color: var(--color-text-primary); cursor: pointer;">发送包</label>
                            </div>
                            <div style="display: flex; align-items: center; gap: 5px;">
                                <input type="checkbox" id="intercept-receive" style="width: 12px; height: 12px;">
                                <label for="intercept-receive" style="font-size: 10px; color: var(--color-text-primary); cursor: pointer;">接收包</label>
                            </div>
                            <div style="margin-top: 4px; display: flex; flex-direction: column; gap: 4px;">
                                <button id="start-intercept" style="padding: 4px 5px; font-size: 10px; background: var(--color-primary); color: white; border: none; border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard); width: 100%)HTML" \
R"HTML(;">开始拦截</button>
                                <button id="stop-intercept" style="padding: 4px 5px; font-size: 10px; background: var(--color-background); color: var(--color-text-primary); border: 1px solid var(--color-border); border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard); width: 100%;">停止拦截</button>
                            </div>
                        </div>
                    </div>
                    
                    
                    <div style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; width: 140px; box-shadow: var(--shadow-sm); display: flex; flex-direction: column;">
                        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
                            <h3 style="font-size: 12px; font-weight: 600; color: var(--color-text-primary); margin: 0;">发送设置</h3>
              )HTML" \
R"HTML(          </div>
                        <div style="display: flex; flex-direction: column; gap: 4px;">
                            <div style="display: flex; justify-content: space-between; align-items: center;">
                                <label for="send-count" style="font-size: 10px; color: var(--color-text-primary);">发送次数:</label>
                                <input type="number" id="send-count" value="1" min="1" class="input" style="width: 40px; font-size: 10px; padding: 3px 5px;">
                            </div>
                            <div style="display: flex; justify-content: space-between; align-items: center;">
                                <label for="send-delay" style="font-size: 10px; color: var(--color-text-primary);">发送延迟:</label>
                                <input type="number" id="send-delay" value="300" min="0" class="input" style="width: 40px; font-size: 10px; padding: 3px 5px;">
                            </div>
                            <d)HTML" \
R"HTML(iv style="margin-top: 4px; display: flex; flex-direction: column; gap: 4px;">
                                <button id="start-send" style="padding: 4px 5px; font-size: 10px; background: var(--color-primary); color: white; border: none; border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard); width: 100%;">开始发送</button>
                                <button id="stop-send" style="padding: 4px 5px; font-size: 10px; background: var(--color-background); color: var(--color-text-primary); border: 1px solid var(--color-border); border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard); width: 100%;">停止发送</button>
                            </div>
                        </div>
                    </div>
                    
                    
                    <div style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; )HTML" \
R"HTML(width: 130px; box-shadow: var(--shadow-sm); display: flex; flex-direction: column;">
                        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
                            <h3 style="font-size: 12px; font-weight: 600; color: var(--color-text-primary); margin: 0;">高级功能</h3>
                        </div>
                        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 4px;">
                            <button id="toggle-packet-window" style="padding: 4px 5px; font-size: 10px; background: var(--color-primary); color: white; border: none; border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">显示封包</button>
                            <button id="clear-all-packets" style="padding: 4px 5px; font-size: 10px; background: var(--color-background); color: var(--color-text-primary); border: 1px solid var(--color-border); border-radius: var(--radius-sm)HTML" \
R"HTML(); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">清空封包</button>
                            <div style="margin: 4px 0; padding: 4px 5px; background: var(--color-background-hover); border-radius: var(--radius-sm); font-size: 10px; color: var(--color-text-secondary); text-align: center; grid-column: 1 / -1;">
                                <div>当前封包个数: <span id="packet-count">0</span></div>
                            </div>
                            <button id="save-packets" style="padding: 4px 5px; font-size: 10px; background: var(--color-background); color: var(--color-text-primary); border: 1px solid var(--color-border); border-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">保存封包</button>
                            <button id="load-packets" style="padding: 4px 5px; font-size: 10px; background: var(--color-background); color: var(--color-text-primary); border: 1px solid var(--color-border); bo)HTML" \
R"HTML(rder-radius: var(--radius-sm); cursor: pointer; transition: all var(--duration-fast) var(--easing-standard);">载入封包</button>
                        </div>
                    </div>
                    
                    
                    <div style="background: var(--color-background); border: 1px solid var(--color-border); border-radius: 4px; padding: 8px; width: 240px; box-shadow: var(--shadow-sm); display: flex; flex-direction: column; flex-shrink: 0;">
                        <div style="margin-bottom: 6px;">
                            <h3 style="font-size: 12px; font-weight: 600; color: var(--color-text-primary); margin: 0;">劫持修改</h3>
                        </div>
                        <div style="display: flex; flex-direction: column; gap: 4px;">
                            <div style="display: flex; align-items: center; gap: 5px;">
                                <input type="checkbox" id="hijack-send" checked style="width: 12px; height: 12px;">
                       )HTML" \
R"HTML(         <label for="hijack-send" style="font-size: 10px; color: var(--color-text-primary); cursor: pointer;">劫持发送</label>
                                <input type="checkbox" id="hijack-recv" checked style="width: 12px; height: 12px;">
                                <label for="hijack-recv" style="font-size: 10px; color: var(--color-text-primary); cursor: pointer;">劫持接收</label>
                            </div>
                            <div style="display: flex; flex-direction: column; gap: 4px;">
                                <input type="text" id="hijack-pattern" placeholder="检测文本（十六进制）" style="width: 100%; font-size: 9px; padding: 3px 5px; border: 1px solid var(--color-border); border-radius: var(--radius-sm); box-sizing: border-box;">
                                <input type="text" id="hijack-replace" placeholder="替换文本" style="width: 100%; font-size: 9px; padding: 3px 5px; border: 1px solid var(--color-border); border-radius: var(--radius-sm); box-sizing: border-box;">)HTML" \
R"HTML(
                            </div>
                            <select id="hijack-type" style="width: 100%; font-size: 10px; padding: 3px 5px; border: 1px solid var(--color-border); border-radius: var(--radius-sm);">
                                <option value="block">拦截</option>
                                <option value="replace">替换</option>
                            </select>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>
    <script>
        // 测试脚本是否执行
        document.title = 'WebView2 Demo - Loaded';

        // 全局函数定义 - 必须在 DOM 元素之前定义
        function openTab(evt, tabName) {
            var tabContents = document.getElementsByClassName('tab-content');
            for (var i = 0; i < tabContents.length; i++) {
                tabContents[i].className = tabContents[i].className.replace(' active', '');
            }
            var tabButtons = document.getElementsByClassName('tab-button');
    )HTML" \
R"HTML(        for (var i = 0; i < tabButtons.length; i++) {
                tabButtons[i].className = tabButtons[i].className.replace(' active', '');
            }
            var activeTab = document.getElementById(tabName);
            activeTab.className += ' active';
            evt.currentTarget.className += ' active';
            
            if (typeof reanimateTabContent === 'function') {
                reanimateTabContent(activeTab);
            }
        }

        function openSubTab(evt, tabName) {
            var tabContents = document.getElementsByClassName('sub-tab-content');
            for (var i = 0; i < tabContents.length; i++) {
                tabContents[i].className = tabContents[i].className.replace(' active', '');
            }
            var tabButtons = document.getElementsByClassName('sub-tab-button');
            for (var i = 0; i < tabButtons.length; i++) {
                tabButtons[i].className = tabButtons[i].className.replace(' active', '');
            }
)HTML" \
R"HTML(            document.getElementById(tabName).className += ' active';
            evt.currentTarget.className += ' active';
            if (tabName === 'sub-taskzone') {
                if (typeof window.updateTaskZoneStatus === 'function') {
                    window.updateTaskZoneStatus(
                        window.isTaskZoneRunning ? '正在执行任务区...' : '等待开始',
                        window.isTaskZoneRunning
                    );
                }
                var huangchengWeijiPanel = document.getElementById('huangcheng-weiji-panel');
                if (huangchengWeijiPanel && !huangchengWeijiPanel.hidden && typeof window.refreshHuangchengWeijiTaskProgress === 'function') {
                    window.refreshHuangchengWeijiTaskProgress();
                }
                if (huangchengWeijiPanel && !huangchengWeijiPanel.hidden && typeof window.renderHuangchengWeijiTasks === 'function') {
                    window.renderHuangchengWeijiTasks();
                }
            }
 )HTML" \
R"HTML(           if (tabName === 'sub-spiritcollect') {
                refreshSpiritPanel();
            }
        }

        // 调试信息接收函数
        window.addBattleData = function(type, data) {
            if (type === 'debug_alert') {
                if (typeof updateHelperInfo === 'function') {
                    updateHelperInfo(data);
                }
            }
        };

        // 辅助提示功能
        function updateHelperInfo(text) {
            const helperText = document.getElementById('helper-text');
            if (helperText) {
                // 添加滑动特效
                helperText.classList.remove('slide-in');
                // 触发重排
                void helperText.offsetWidth;
                // 更新文本
                helperText.textContent = text;
                // 添加滑动特效类
                helperText.classList.add('slide-in');
            }
        }

        function resetAnimation(element) {
            // 重置动画效果
            element.style.animation = 'none';
            element.)HTML" \
R"HTML(offsetHeight; // 强制重排
            element.style.animation = '';
        }
        
        function reanimateTabContent(tabContent) {
            // 重新触发当前标签页内所有元素的动画
            var elements = tabContent.querySelectorAll('.section-title, .control-group, .input, .btn, .list-view, .list-item, .form-row, .divider, .panel-animate');
            
            // 设置递增的延迟时间
            var delay = 0.02;
            for (var i = 0; i < elements.length; i++) {
                var element = elements[i];
                resetAnimation(element);
                
                // 根据元素类型设置不同的动画延迟
                if (element.classList.contains('section-title')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.02;
                } else if (element.classList.contains('control-group') || element.classList.contains('form-row')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.02;
                } else if ()HTML" \
R"HTML(element.classList.contains('input') || element.classList.contains('btn')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.01;
                } else if (element.classList.contains('list-view')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.02;
                } else if (element.classList.contains('list-item')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.01;
                } else if (element.classList.contains('divider')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.01;
                } else if (element.classList.contains('panel-animate')) {
                    element.style.animationDelay = delay + 's';
                    delay += 0.015;
                }
            }
        }
        
        // 绑定基础功能按钮事件
        var refreshGameBtn = document.getElementById('refresh-game');
   )HTML" \
R"HTML(     var refreshNoLoginBtn = document.getElementById('refresh-no-login');
        var muteGameBtn = document.getElementById('mute-game');
        var clearIECacheBtn = document.getElementById('clear-ie-cache');
        var copyKeyBtn = document.getElementById('copy-key');
        var keyLoginBtn = document.getElementById('key-login');
        var clearIECacheResetTimer = null;

        // 刷新游戏按钮
        if (refreshGameBtn) {
            refreshGameBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('refresh-game');
                }
            });
        }

        // 复制 Key 按钮
        if (copyKeyBtn) {
            copyKeyBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({ type: 'copy-login-key' });
                }
            });
        }

        // Key 登录按钮
        )HTML" \
R"HTML(if (keyLoginBtn) {
            keyLoginBtn.addEventListener('click', function() {
                showKeyLoginDialog();
            });
        }

        // 不换号刷新按钮
        if (refreshNoLoginBtn) {
            refreshNoLoginBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('refresh-no-login');
                }
            });
        }

        // 游戏静音按钮
        if (muteGameBtn) {
            muteGameBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('mute-game');
                }
            });
        }

        // 清理 IE 缓存按钮
        if (clearIECacheBtn) {
            clearIECacheBtn.addEventListener('click', function() {
                var clearCacheText = document.getElementById('clear-cache-text');

                if (!clearCacheText || !window.chrome || !window)HTML" \
R"HTML(.chrome.webview) {
                    return;
                }

                if (clearIECacheResetTimer) {
                    clearTimeout(clearIECacheResetTimer);
                    clearIECacheResetTimer = null;
                }

                clearIECacheBtn.disabled = true;
                clearIECacheBtn.classList.remove('cache-cleared', 'cache-clear-failed');
                clearIECacheBtn.classList.add('cache-clearing');
                clearCacheText.textContent = '清理中';
                window.chrome.webview.postMessage('clear-ie-cache');
            });
        }
        
        // 更新静音按钮状态的函数
        window.updateMuteButtonState = function(isMuted) {
            var muteGameBtn = document.getElementById('mute-game');
            var muteText = document.getElementById('mute-text');
            
            if (muteGameBtn && muteText) {
                if (isMuted) {
                    muteGameBtn.classList.add('muted');
                    muteText.textContent = )HTML" \
R"HTML('恢复声音';
                } else {
                    muteGameBtn.classList.remove('muted');
                    muteText.textContent = '游戏静音';
                }
            }
        };

        window.onClearIECacheFinished = function(success) {
            var clearIECacheBtn = document.getElementById('clear-ie-cache');
            var clearCacheText = document.getElementById('clear-cache-text');

            if (!clearIECacheBtn || !clearCacheText) {
                return;
            }

            clearIECacheBtn.disabled = false;
            clearIECacheBtn.classList.remove('cache-clearing');
            clearIECacheBtn.classList.toggle('cache-cleared', !!success);
            clearIECacheBtn.classList.toggle('cache-clear-failed', !success);
            clearCacheText.textContent = success ? '已清理' : '清理失败';

            if (clearIECacheResetTimer) {
                clearTimeout(clearIECacheResetTimer);
            }

            clearIECacheResetTimer = setTimeout(function() {
 )HTML" \
R"HTML(               clearIECacheBtn.classList.remove('cache-cleared', 'cache-clear-failed');
                clearCacheText.textContent = '清理缓存';
            }, 1800);
        };
        
        // 定义全局函数，确保 C++ 端可以随时调用
        window.addPacketToList = function(index, direction, hex, time, label) {
            var pListItems = document.getElementById('packet-list-items');
            if (pListItems) {
                var packetItem = document.createElement('div');
                packetItem.className = 'packet-item';
                packetItem.setAttribute('data-hex', hex);
                packetItem.setAttribute('data-index', index - 1);  // 修改：使用从0开始的数组索引

                // 确保direction是简短的形式
                var displayDirection = (direction === '发送包' || direction === '发送') ? '发送' : '接收';
                var isSend = displayDirection === '发送';

                packetItem.innerHTML = `
                    <div style="padding: 6px 8px; border-bottom: 1px solid var(--color-border-hover); cu)HTML" \
R"HTML(rsor: pointer; transition: background 0.15s ease;">
                        
                        <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 4px;">
                            <div style="width: 25px; flex-shrink: 0; font-weight: 600; color: var(--color-text-secondary); font-size: 9px; text-align: center;">
                                ${index}.
                            </div>
                            <div style="width: 30px; flex-shrink: 0; font-weight: 600; color: ${isSend ? '#0078d4' : '#2e7d32'}; font-size: 9px; background: ${isSend ? 'rgba(0, 120, 212, 0.08)' : 'rgba(46, 125, 50, 0.08)'}; padding: 2px 4px; border-radius: 2px; text-align: center;">
                                ${displayDirection}
                            </div>
                            <div style="width: 50px; flex-shrink: 0; color: var(--color-text-secondary); font-size: 8px; font-family: 'Consolas', monospace;">
                                ${time}
           )HTML" \
R"HTML(                 </div>
                            <div style="flex: 1; min-width: 0; color: var(--color-primary); font-size: 8px; font-weight: 500; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">
                                ${label || '-'}
                            </div>
                        </div>
                        
                        <div style="color: var(--color-text-primary); font-family: 'Consolas', monospace; font-size: 8px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; padding-left: 4px;">
                            ${hex}
                        </div>
                    </div>
                `;

                // 添加点击事件
                packetItem.addEventListener('click', function() {
                    // 移除其他选中状态
                    var allItems = pListItems.querySelectorAll('.packet-item');
                    allItems.forEach(function(item) {
                        item.style.background = '';
               )HTML" \
R"HTML(         item.style.borderLeft = '';
                    });
                    // 设置当前选中状态
                    packetItem.style.background = 'rgba(0, 120, 212, 0.08)';
                    packetItem.style.borderLeft = '2.1px solid #0078d4';
                    // 填充到输入框
                    var hexInput = document.getElementById('packet-hex-input');
                    if (hexInput) {
                        hexInput.value = hex;
                    }
                });

                pListItems.appendChild(packetItem);
                pListItems.scrollTop = pListItems.scrollHeight; // 自动滚动到底底
            }
        };
        window.addPacketToUI = window.addPacketToList; // 兼容性别名

        // 灵玉列表更新功能
        window.currentLingyuData = null;
        window.selectedLingyuIndices = new Set();
        window.currentLingyuData = { items: [] };

        window.updateLingyuUI = function(jsonStr) {
            try {
                const data = typeof jsonStr === 'string' ? JSON.parse(jso)HTML" \
R"HTML(nStr) : jsonStr;
                window.currentLingyuData = data;
                // 刷新数据时，保留已选中的索引，但要过滤掉已经不存在的索引
                if (data.items) {
                    const currentIndices = new Set(data.items.map(item => String(item.symmIndex)));
                    for (let idx of window.selectedLingyuIndices) {
                        if (!currentIndices.has(idx)) {
                            window.selectedLingyuIndices.delete(idx);
                        }
                    }
                }
                window.renderLingyuList(data.items);
                window.updateSelectedLingyuDisplay();
            } catch (e) {
            }
        };

        window.renderLingyuList = function(items) {
            const listContainer = document.getElementById('lingyu-list');
            const countElement = document.getElementById('lingyu-count');
            
            if (!listContainer) return;
            
            listContainer.innerHTML = '';
            if (!items ||)HTML" \
R"HTML( items.length === 0) {
                listContainer.innerHTML = '<div style="text-align: center; padding: 20px; color: var(--color-text-tertiary); font-size: 10.5px;">暂无灵玉数据</div>';
                if (countElement) countElement.textContent = '0 个灵玉';
                return;
            }
            
            if (countElement) countElement.textContent = items.length + ' 个灵玉';
            
            items.forEach(item => {
                const isSelected = window.selectedLingyuIndices.has(String(item.symmIndex));
                const row = document.createElement('div');
                row.style.display = 'grid';
                row.style.gridTemplateColumns = '55px 40px 65px 1fr';
                row.style.padding = '3px 6px';
                row.style.borderBottom = '1px solid var(--color-border)';
                row.style.alignItems = 'center';
                row.style.minHeight = '24px';
                row.style.transition = 'background-color 0.1s linear';
              )HTML" \
R"HTML(  row.style.cursor = 'pointer';
                row.style.backgroundColor = isSelected ? 'var(--color-background-secondary)' : 'transparent';
                
                row.onclick = () => window.selectLingyu(item);
                row.onmouseover = () => {
                    if (!window.selectedLingyuIndices.has(String(item.symmIndex))) {
                        row.style.backgroundColor = 'var(--color-background-hover)';
                    }
                };
                row.onmouseout = () => {
                    if (!window.selectedLingyuIndices.has(String(item.symmIndex))) {
                        row.style.backgroundColor = 'transparent';
                    } else {
                        row.style.backgroundColor = 'var(--color-background-secondary)';
                    }
                };
                
                const petName = item.petName || '无';
                const petColor = item.petName ? 'var(--color-primary)' : 'var(--color-text-tertiary)';
)HTML" \
R"HTML(                const symmName = item.symmName || '未知';
                
                let attrHtml = '';
                if (item.nativeList && item.nativeList.length > 0) {
                    item.nativeList.forEach(attr => {
                        attrHtml += `<span style="display: inline-block; background: #e8f5e8; color: #2e7d32; padding: 1px 3px; border-radius: 2px; margin-right: 2px; font-weight: 500; font-size: 9.5px;">${attr.nativeName}:${attr.nativeValue}</span>`;
                    });
                } else {
                    attrHtml = '<span style="color: var(--color-text-tertiary); font-size: 10.5px;">无</span>';
                }
                
                row.innerHTML = `
                    <div style="text-align: center; color: var(--color-text-primary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; font-size: 10.5px;">${symmName}</div>
                    <div style="text-align: center; color: var(--color-text-secondary); font-size: 1)HTML" \
R"HTML(0.5px;">${item.symmIndex}</div>
                    <div style="text-align: center; color: ${petColor}; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; font-size: 10.5px;">${petName}</div>
                    <div style="display: flex; flex-wrap: wrap; gap: 2px; font-size: 9.5px;">${attrHtml}</div>
                `;
                
                listContainer.appendChild(row);
            });
        };

        window.selectLingyu = function(item) {
            const indexStr = String(item.symmIndex);
            if (window.selectedLingyuIndices.has(indexStr)) {
                window.selectedLingyuIndices.delete(indexStr);
            } else {
                window.selectedLingyuIndices.add(indexStr);
                // 自动将选中的灵玉添加到分解队列
                addToDecomposeQueueFromSelection(item);
                // 添加后自动取消选中状态
                setTimeout(() => {
                    window.selectedLingyuIndices.delete(indexStr);
                    // 重新渲染列表以更新选中状态背景色
  )HTML" \
R"HTML(                  window.renderLingyuList(window.currentLingyuData.items);
                    // 更新右侧显示
                    window.updateSelectedLingyuDisplay();
                }, 300); // 延迟300ms取消选中，让用户能看到添加动作
            }
            
            // 重新渲染列表以更新选中状态背景色
            window.renderLingyuList(window.currentLingyuData.items);
            // 更新右侧显示
            window.updateSelectedLingyuDisplay();
        };
        
        // 从选择直接添加到分解队列的函数
        window.addToDecomposeQueueFromSelection = function(item) {
            const indexStr = String(item.symmIndex);

            // 检查是否已存在于分解队列中
            const exists = window.decomposeQueue.some(q => q.index === indexStr);
            if (!exists) {
                window.decomposeQueue.push({
                    index: indexStr,
                    name: item.symmName || '未知',
                    level: item.symmLevel || 'N',
                    attrs: item.nativeList ? item.nativeList.map(attr => `${attr.nativeName}:${attr)HTML" \
R"HTML(.nativeValue}`).join(',') : ''
                });

                // 重新渲染分解队列
                window.renderDecomposeQueue();
            }
        };

        window.updateSelectedLingyuDisplay = function() {
            const infoDiv = document.getElementById('selected-lingyu-info');
            if (!infoDiv) return;
            
            if (window.selectedLingyuIndices.size === 0) {
                infoDiv.innerHTML = '请从左侧列表选择灵玉';
                return;
            }
            
            let html = `<div style="font-weight: 600; color: var(--color-primary); margin-bottom: 4px; border-bottom: 1px solid var(--color-border); padding-bottom: 2px;">已选择 (${window.selectedLingyuIndices.size})</div>`;
            
            // 找到对应的灵玉对象并显示
            const selectedItems = (window.currentLingyuData.items || []).filter(item => 
                window.selectedLingyuIndices.has(String(item.symmIndex))
            );
            
            selectedItems.forEach(item => {
        )HTML" \
R"HTML(        let attrs = item.nativeList.map(a => `${a.nativeName}:${a.nativeValue}`).join(',');
                html += `
                    <div style="margin-bottom: 4px; padding: 2px; border-radius: 2px; background: rgba(0,0,0,0.02); border: 1px solid rgba(0,0,0,0.05);">
                        <div style="display: flex; justify-content: space-between;">
                            <span style="font-weight: 500;">${item.symmName}</span>
                            <span style="color: var(--color-text-tertiary);">#${item.symmIndex}</span>
                        </div>
                        <div style="font-size: 8.5px; color: var(--color-text-secondary);">${attrs || '无属性'}</div>
                    </div>
                `;
            });
            
            infoDiv.innerHTML = html;
        };

        window.queryLingyu = function() {
            // 调用C++端的灵玉查询函数
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
    )HTML" \
R"HTML(                type: 'query_lingyu'
                });
            }
        };

        // 分解队列功能
        window.decomposeQueue = []; // 存储要分解的灵玉对象 {index, name}
        window.selectedLingyuIndices = new Set(); // 左侧列表选中项
        window.isDecomposing = false; // 是否正在分解



        window.clearDecomposeQueue = function() {
            if (!window.decomposeQueue || window.decomposeQueue.length === 0) {
                return;
            }

            // 不使用confirm，直接清空队列
            window.decomposeQueue = [];
            window.renderDecomposeQueue();
        };

        // 从分解队列中移除单个项目
        window.removeItemFromDecomposeQueue = function(index) {
            // 从队列中移除指定索引的项目
            window.decomposeQueue = window.decomposeQueue.filter(q => q.index !== index);

            // 重新渲染队列
            window.renderDecomposeQueue();
        };

        window.renderDecomposeQueue = function() {
            try {
                var listContainer = document.getElementById('decompose-l)HTML" \
R"HTML(ist');
                var countElement = document.getElementById('decompose-queue-count');
                
                if (!listContainer) return;
                
                if (!window.decomposeQueue) {
                    window.decomposeQueue = [];
                }
                
                if (window.decomposeQueue.length === 0) {
                    listContainer.innerHTML = '<div style="text-align:center;padding:10px;color:var(--color-text-tertiary);font-size:10.5px;">未添加灵玉</div>';
                    if (countElement) countElement.textContent = '已添加: 0 个灵玉';
                    return;
                }
                
                if (countElement) countElement.textContent = '已添加: ' + window.decomposeQueue.length + ' 个灵玉';
                
                // 使用flexbox布局，一行显示两个项目
                var html = '<div style="display:flex;flex-wrap:wrap;gap:3px;">';
                for (var i = 0; i < window.decomposeQueue.length; i++) {
                    var )HTML" \
R"HTML(item = window.decomposeQueue[i];
                    // 每个项目包含选择框、序号、名称、属性和删除按钮
                    html += '<div style="flex:1;min-width:130px;max-width:220px;display:flex;align-items:center;padding:3px 5px;border:1px solid var(--color-border);border-radius:3px;align-items:center;gap:4px;font-size:9.5px;">';
                    // 选择框
                    html += '<input type="checkbox" class="queue-item-checkbox" data-index="' + item.index + '" style="width:12px;height:12px;flex-shrink:0;" />';
                    // 序号
                    html += '<span style="display:inline-block;background:var(--color-background-secondary);color:var(--color-text-primary);padding:1px 4px;border-radius:2px;font-size:9.5px;font-weight:500;white-space:nowrap;flex-shrink:0;">#' + item.index + '</span>';
                    // 名称
                    html += '<span style="color:var(--color-text-primary);font-size:10.5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;flex:1;min-width:0;">' + (it)HTML" \
R"HTML(em.name || '未知') + '</span>';
                    // 属性（简约显示）
                    if (item.attrs && item.attrs.length > 0) {
                        html += '<span style="display:inline-block;background:#e8f5e8;color:#2e7d32;padding:1px 3px;border-radius:2px;font-weight:500;font-size:8.5px;flex-shrink:0;">' + item.attrs + '</span>';
                    }
                    // 美化的删除按钮
                    html += '<button onclick="window.removeItemFromDecomposeQueue(\'' + item.index + '\')" style="width:16px;height:16px;border:none;border-radius:50%;background:var(--color-text-tertiary);color:white;font-size:9.5px;cursor:pointer;display:flex;align-items:center;justify-content:center;font-weight:bold;">×</button>';
                    html += '</div>';
                }
                html += '</div>';
                listContainer.innerHTML = html;
            } catch (e) {
            }
        };

        window.decomposeAllInQueue = function() {
            try {
                // )HTML" \
R"HTML(初始化队列
                if (!window.decomposeQueue) {
                    window.decomposeQueue = [];
                }

                // 检查是否正在分解
                if (window.isDecomposing) {
                    // 更新辅助提示文本
                    if (window.updateHelperText) {
                        window.updateHelperText('正在分解中，请稍候...');
                    }
                    return;
                }

                // 检查队列是否为空
                if (window.decomposeQueue.length === 0) {
                    // 更新辅助提示文本
                    if (window.updateHelperText) {
                        window.updateHelperText('分解队列为空，请先添加灵玉');
                    }
                    return;
                }

                var count = window.decomposeQueue.length;

                // 直接继续执行，不使用confirm对话框，因为可能导致卡死

                // 标记为正在分解
                window.isDecomposing = true;

                // 收集索引
                var indices = [];
                for (var i = 0; i < window.decom)HTML" \
R"HTML(poseQueue.length; i++) {
                    indices.push(window.decomposeQueue[i].index);
                }

                // 发送消息
                if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                    // 构建分解请求
                    const request = {
                        type: 'decompose_lingyu_batch',
                        indices: indices
                    };

                    // 模拟构建分解封包的十六进制表示
                    const hexPacket = buildDecomposePacketHex(indices);

                    window.chrome.webview.postMessage(request);
                } else {
                    // 更新辅助提示文本
                    if (window.updateHelperText) {
                        window.updateHelperText('错误: WebView2 API不可用');
                    }
                    window.isDecomposing = false;
                }
            } catch (e) {
                window.isDecomposing = false;
                // 更新辅助提示文本
                if (window.updat)HTML" \
R"HTML(eHelperText) {
                    window.updateHelperText('分解过程中出现错误: ' + e.message);
                }
            }
        };
        
        // 模拟构建分解封包的十六进制表示
        window.buildDecomposePacketHex = function(indices) {
            try {
                // 将索引数组转换为字符串，例如 ["62", "61"] -> "62_61"
                const indicesStr = indices.join('_');

                // 解析第一个索引并转换为整数（用于模拟）
                const firstIndex = parseInt(indices[0]); // 使用第一个索引作为示例

                // 构建十六进制表示 (按照正确的协议格式 - 使用整数格式)
                let hex = "";

                // 1. Magic: "SD" (2字节)
                hex += "44 53 ";

                // 2. Length field (2字节，固定为00 00)
                hex += "00 00 ";

                // 3. Opcode: 0x00121816 (4字节)
                hex += "16 18 12 00 ";

                // 4. Index as 8-byte integer in little endian format
                // 例如，如果索引是500 (0x01F4):
                // 0xF4 0x01 0x00 0x00 0x00 0x00 0x00 0x00
                for (let i = 0; i <)HTML" \
R"HTML( 8; i++) {
                    hex += ((firstIndex >> (i * 8)) & 0xFF).toString(16).padStart(2, '0').toUpperCase();
                    if (i < 7) hex += " ";
                }
                hex += " ";

                // 5. Unknown bytes: 00 00 (2字节)
                hex += "00 00";

                return hex.trim();
            } catch (e) {
                        return "构建失败";
                    }        };

        // 处理C++端返回的分解完成消息
        window.onDecomposeComplete = function() {
            // 清空队列
            window.decomposeQueue = [];
            window.renderDecomposeQueue();

            // 清空左侧选中
            window.selectedLingyuIndices.clear();

            // 安全地重新渲染灵玉列表
            if (window.currentLingyuData && window.currentLingyuData.items) {
                window.renderLingyuList(window.currentLingyuData.items);
            } else {
                window.renderLingyuList([]);
            }

            window.isDecomposing = false;

            // 发送查询请求刷新)HTML" \
R"HTML(列表
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({type: 'query_lingyu'});
            }
        };

        // ============== 日常活动功能 ==============
        window.isDailyTaskRunning = false;
        window.isTaskZoneRunning = false;

        // 更新深度挖宝次数显示
        window.updateDeepDigCount = function(count) {
            var countEl = document.getElementById('deep-dig-count');
            if (countEl) {
                countEl.textContent = count >= 0 ? ('剩余' + count + '次') : '-';
            }
        };

        // 更新日常活动状态显示
        window.updateDailyTaskStatus = function(text, isRunning) {
            var statusEl = document.getElementById('daily-task-status');
            if (statusEl) {
                statusEl.textContent = text;
                statusEl.style.color = isRunning ? 'var(--color-primary)' : 'var(--color-text-secondary)';
            }
            window.isDailyTaskRunning = isRunning;
            
       )HTML" \
R"HTML(     // 更新所有相关按钮状态
            var dailyBtn = document.getElementById('complete-daily-tasks');
            var trialBtn = document.getElementById('complete-trial-tasks');
            var danceBtn = document.getElementById('complete-dance-tasks');
            
            if (dailyBtn) {
                dailyBtn.disabled = isRunning;
                dailyBtn.textContent = isRunning ? '执行中...' : '一键完成';
            }
            if (trialBtn) {
                trialBtn.disabled = isRunning;
                trialBtn.textContent = isRunning ? '执行中...' : '一键试炼';
            }
            if (danceBtn) {
                danceBtn.disabled = isRunning;
                danceBtn.textContent = isRunning ? '执行中...' : '一键跳舞';
            }
        };

        // 一键完成日常活动
        window.completeDailyTasks = function() {
            if (window.isDailyTaskRunning) return;
            
            // 收集选中的任务（新增活动）- 只处理每日模块
            var flags = 0;
            if (document.getElementById('daily-deep-d)HTML" \
R"HTML(ig').checked) flags |= 0x01;   // 深度挖宝
            if (document.getElementById('daily-card').checked) flags |= 0x02;       // 每日卡牌
            if (document.getElementById('daily-gift').checked) flags |= 0x04;       // 每日礼包
            if (document.getElementById('weekly-gift').checked) flags |= 0x08;      // 每周礼包
            if (document.getElementById('family-checkin').checked) flags |= 0x10;   // 家族考勤
            if (document.getElementById('family-report').checked) flags |= 0x20;    // 家族报道
            if (document.getElementById('family-defend').checked) flags |= 0x40;    // 家族保卫
            if (document.getElementById('shop-surprise').checked) flags |= 0x80;    // 商城惊喜
            
            if (flags === 0) {
                window.updateDailyTaskStatus('请至少勾选一项日常活动', false);
                window.updateHelperText('请至少勾选一项日常活动');
                return;
            }
            
            window.updateDailyTaskStatus('正在执行日常活动，请勿操作...', true);
            window.updateHelpe)HTML" \
R"HTML(rText('正在执行日常活动，请勿操作...');
            
            // 发送到C++端
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'daily_tasks',
                    flags: flags
                });
            }
        };

        // 一键完成试炼活动
        window.completeTrialTasks = function() {
            if (window.isDailyTaskRunning) return;
            
            // 只收集试炼相关的任务
            var flags = 0;
            var trialNames = [];
            if (document.getElementById('daily-fire-wind').checked) {
                flags |= 0x200;
                trialNames.push('火风试炼');
            }
            if (document.getElementById('daily-fire').checked) {
                flags |= 0x400;
                trialNames.push('火焰试炼');
            }
            if (document.getElementById('daily-storm').checked) {
                flags |= 0x800;
                trialNames.push('风暴试炼');
            }
            
            i)HTML" \
R"HTML(f (flags === 0) {
                window.updateDailyTaskStatus('请至少勾选一项试炼活动', false);
                window.updateHelperText('请至少勾选一项试炼活动');
                return;
            }
            
            window.updateDailyTaskStatus('正在执行 ' + trialNames.join('、') + '...', true);
            window.updateHelperText('正在执行 ' + trialNames.join('、') + '...');
            
            // 发送到C++端
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'daily_tasks',
                    flags: flags
                });
            }
        };

        // 玄塔功能 - 一键跑塔（易语言风格，盲投20次）
        window.oneKeyXuantta = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'one_key_xuantta'
                }));
            }
        };

        // 玄塔功能 - 购买18个骰子
        window.buyDice18 = function() {
           )HTML" \
R"HTML( if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'buy_dice_18'
                }));
            }
        };

        // 双台谷刷级 - 查询妖怪数据
        window.queryShuangTai = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'query_shuangtai'
                }));
            }
        };

        // 双台谷刷级 - 一键启动
        window.startShuangTai = function() {
            var blockBattleCb = document.getElementById('shuangtai-block-battle');
            var blockBattle = blockBattleCb ? blockBattleCb.checked : false;
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'start_shuangtai',
                    blockBattle: blockBattle
                }));
                // 更新按钮状态
            )HTML" \
R"HTML(    var startBtn = document.getElementById('start-shuangtai');
                var stopBtn = document.getElementById('stop-shuangtai');
                if (startBtn) startBtn.disabled = true;
                if (stopBtn) stopBtn.disabled = false;
            }
        };

        // 双台谷刷级 - 停止
        window.stopShuangTai = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'stop_shuangtai'
                }));
                // 更新按钮状态
                var startBtn = document.getElementById('start-shuangtai');
                var stopBtn = document.getElementById('stop-shuangtai');
                if (startBtn) startBtn.disabled = false;
                if (stopBtn) stopBtn.disabled = true;
            }
        };
        
        // 双台谷刷级 - 更新妖怪和技能下拉框
        window.updateShuangTaiUI = function(petName, skillName, petCount, canStart) {
            var petSelect = docume)HTML" \
R"HTML(nt.getElementById('shuangtai-pet');
            var skillSelect = document.getElementById('shuangtai-skill');
            var startBtn = document.getElementById('start-shuangtai');
            var stopBtn = document.getElementById('stop-shuangtai');
            
            if (petSelect) {
                petSelect.innerHTML = '<option value="">' + petName + '</option>';
                petSelect.disabled = false;
            }
            if (skillSelect) {
                skillSelect.innerHTML = '<option value="">' + skillName + '</option>';
                skillSelect.disabled = false;
            }
            if (startBtn) {
                startBtn.disabled = !canStart;
            }
            if (stopBtn) {
                stopBtn.disabled = true;  // 初始状态停止按钮禁用
            }
        };

        window.huangchengWeijiTaskDefs = [
            { subtaskId: 2001001, name: '接受使命', sceneId: 1003, sceneName: '驾驶舱', reward: '600铜钱 / 700历练', note: '起始报名' },
            { subtaskId: 2)HTML" \
R"HTML(001002, name: '奇怪的声音', sceneId: 2003, sceneName: '双叉岭', reward: '700铜钱 / 800历练', note: '第一次下沉' },
            { subtaskId: 2001003, name: '歪打正着揭皇榜', sceneId: 2001, sceneName: '长安城', reward: '800铜钱 / 1000历练', note: '进入大唐主线' },
            { subtaskId: 2001004, name: '唐太宗的回忆', sceneId: 2002, sceneName: '皇宫', reward: '800铜钱 / 800历练', note: '皇城核心' },
            { subtaskId: 2001005, name: '危机重重', sceneId: 2003, sceneName: '双叉岭', reward: '900铜钱 / 1000历练', note: '含 200100504 特殊镜头' },
            { subtaskId: 2001006, name: '传说中的唐三藏', sceneId: 2003, sceneName: '双叉岭', reward: '800铜钱 / 1200历练', note: '救援段' },
            { subtaskId: 2001007, name: '你逃不掉的！', sceneId: 2005, sceneName: '五指山顶', reward: '900铜钱 / 1200历练', note: '地形互动' },
            { subtaskId: 2001008, name: '神仙的指示', sceneId: 2005, sceneName: '五指山顶', reward: '700铜钱 / 1000历练 / 100修为 / 50感恩', note: '收尾' }
        ];

        window.huangchengWeijiState = {
            hideCompleted: true,
            isRunning: false,
            s)HTML" \
R"HTML(electedMap: {},
            acceptedMap: {},
            completedMap: {}
        };

        function buildIdMap(values) {
            var map = {};
            if (!values || !values.length) {
                return map;
            }
            for (var i = 0; i < values.length; i++) {
                map[String(values[i])] = true;
            }
            return map;
        }

        function countMapValues(map) {
            var count = 0;
            for (var key in map) {
                if (Object.prototype.hasOwnProperty.call(map, key) && map[key]) {
                    count++;
                }
            }
            return count;
        }

        function escapeHtml(text) {
            return String(text)
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;')
                .replace(/"/g, '&quot;')
                .replace(/'/g, '&#39;');
        }

        function renderHuangchengWeijiTasks() {
     )HTML" \
R"HTML(       var listEl = document.getElementById('huangcheng-weiji-list');
            var summaryEl = document.getElementById('huangcheng-weiji-summary');
            if (!listEl) {
                return;
            }

            var state = window.huangchengWeijiState || {};
            var hideCompleted = state.hideCompleted !== false;
            var rows = [];
            var completedCount = 0;
            var acceptedCount = 0;
            var selectedCount = 0;

            for (var i = 0; i < window.huangchengWeijiTaskDefs.length; i++) {
                var task = window.huangchengWeijiTaskDefs[i];
                var key = String(task.subtaskId);
                var isCompleted = !!state.completedMap[key];
                var isAccepted = !!state.acceptedMap[key];
                var isSelected = !!state.selectedMap[key];

                if (isCompleted) {
                    completedCount++;
                }
                if (isAccepted) {
                    acceptedCoun)HTML" \
R"HTML(t++;
                }
                if (isSelected) {
                    selectedCount++;
                }

                if (hideCompleted && isCompleted) {
                    continue;
                }

                var statusText = isCompleted ? '已完成' : (isAccepted ? '进行中' : '待执行');
                var rowBackground = isSelected ? 'rgba(43, 125, 255, 0.06)' : 'transparent';
                var checkboxDisabled = isCompleted ? 'disabled' : '';
                var checkboxChecked = isSelected ? 'checked' : '';
                rows.push(
                    '<div style="display:grid; grid-template-columns: 30px 1.2fr 64px 120px 90px; align-items:center; padding: 4px 6px; border-bottom: 1px solid var(--color-border); background:' +
                    rowBackground + ';">' +
                    '<div style="display:flex; justify-content:center;"><input type="checkbox" ' + checkboxChecked + ' ' + checkboxDisabled + ' style="width: 12px; height: 12px; cursor: pointer;" onchang)HTML" \
R"HTML(e="window.setHuangchengWeijiSelection(' + task.subtaskId + ', this.checked)"></div>' +
                    '<div style="display:flex; flex-direction:column; gap:1px; min-width:0;"><div style="font-size: 10px; font-weight: 600; color: var(--color-text-primary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">' + escapeHtml(task.name) + '</div><div style="font-size: 8.5px; color: var(--color-text-tertiary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">' + escapeHtml(task.note) + '</div></div>' +
                    '<div style="font-size: 9px; color: var(--color-text-secondary); text-align: center;">' + escapeHtml(task.sceneName) + '<div style="font-size: 8.5px; color: var(--color-text-tertiary);">' + task.sceneId + '</div></div>' +
                    '<div style="font-size: 9px; color: var(--color-text-secondary); text-align: center; line-height: 1.3;">' + escapeHtml(task.reward) + '</div>' +
                    '<div style="font-size: 9px; color: ')HTML" \
R"HTML( + (isCompleted ? 'var(--color-primary)' : 'var(--color-text-secondary)') + '; text-align: center;">' + statusText + '</div>' +
                    '</div>'
                );
            }

            if (rows.length === 0) {
                rows.push('<div style="text-align:center; padding: 12px; color: var(--color-text-tertiary);">没有可显示的任务</div>');
            }

            listEl.innerHTML = rows.join('');

            if (summaryEl) {
                summaryEl.textContent = '已完成: ' + completedCount + ' / ' + window.huangchengWeijiTaskDefs.length +
                    '，已接: ' + acceptedCount + '，已勾选: ' + selectedCount;
            }

            var startBtn = document.getElementById('start-huangcheng-weiji');
            if (startBtn) {
                startBtn.disabled = selectedCount === 0;
                startBtn.textContent = state.isRunning ? '执行中...' : '完成所选';
            }

            var stopBtn = document.getElementById('stop-huangcheng-weiji');
            if (stopBt)HTML" \
R"HTML(n) {
                stopBtn.disabled = !state.isRunning;
            }
        }

        window.setHuangchengWeijiSelection = function(subtaskId, checked) {
            var state = window.huangchengWeijiState;
            if (!state) {
                return;
            }
            var key = String(subtaskId);
            if (checked) {
                state.selectedMap[key] = true;
            } else {
                delete state.selectedMap[key];
            }
            renderHuangchengWeijiTasks();
        };

        window.selectAllHuangchengWeiji = function() {
            var state = window.huangchengWeijiState;
            if (!state) {
                return;
            }
            state.selectedMap = {};
            for (var i = 0; i < window.huangchengWeijiTaskDefs.length; i++) {
                var task = window.huangchengWeijiTaskDefs[i];
                if (!state.completedMap[String(task.subtaskId)]) {
                    state.selectedMap[String(task.subtaskI)HTML" \
R"HTML(d)] = true;
                }
            }
            renderHuangchengWeijiTasks();
        };

        window.clearHuangchengWeijiSelection = function() {
            var state = window.huangchengWeijiState;
            if (!state) {
                return;
            }
            state.selectedMap = {};
            renderHuangchengWeijiTasks();
        };

        window.toggleHuangchengWeijiHideCompleted = function(enabled) {
            var state = window.huangchengWeijiState;
            if (!state) {
                return;
            }
            state.hideCompleted = !!enabled;
            renderHuangchengWeijiTasks();
        };

        window.updateHuangchengWeijiProgress = function(payload) {
            var state = window.huangchengWeijiState;
            if (!state || !payload) {
                return;
            }
            state.acceptedMap = buildIdMap(payload.acceptedSubtaskIds || []);
            state.completedMap = buildIdMap(payload.finishedSubtaskIds ||)HTML" \
R"HTML( []);
            for (var key in state.selectedMap) {
                if (Object.prototype.hasOwnProperty.call(state.selectedMap, key) && state.completedMap[key]) {
                    delete state.selectedMap[key];
                }
            }
            renderHuangchengWeijiTasks();
        };

        window.updateHuangchengWeijiStatus = function(text, isRunning) {
            var state = window.huangchengWeijiState;
            if (state) {
                state.isRunning = !!isRunning;
            }
            var statusEl = document.getElementById('huangcheng-weiji-status');
            if (statusEl) {
                statusEl.textContent = text;
                statusEl.style.color = isRunning ? 'var(--color-primary)' : 'var(--color-text-secondary)';
            }

            var startBtn = document.getElementById('start-huangcheng-weiji');
            var stopBtn = document.getElementById('stop-huangcheng-weiji');
            if (startBtn) {
                startBtn.disa)HTML" \
R"HTML(bled = isRunning || countMapValues(state ? state.selectedMap : {}) === 0;
            }
            if (stopBtn) {
                stopBtn.disabled = !isRunning;
            }
        };

        window.refreshHuangchengWeijiTaskProgress = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'query_huangcheng_weiji_task_progress',
                    task_id: 2001000
                });
            }
        };

        window.startHuangchengWeijiTask = function() {
            var state = window.huangchengWeijiState;
            if (!state) {
                return;
            }

            var selectedIds = [];
            for (var key in state.selectedMap) {
                if (Object.prototype.hasOwnProperty.call(state.selectedMap, key) && state.selectedMap[key]) {
                    selectedIds.push(parseInt(key, 10));
                }
            }
            selectedIds.sort(func)HTML" \
R"HTML(tion(a, b) { return a - b; });

            if (selectedIds.length === 0) {
                window.updateHelperText('请先勾选皇城危机任务');
                return;
            }

            window.updateHuangchengWeijiStatus('正在同步皇城危机进度...', true);
            window.updateHelperText('正在同步皇城危机进度...');

            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'start_huangcheng_weiji_task',
                    task_id: 2001000,
                    subtask_ids: selectedIds
                });
            }
        };

        window.stopHuangchengWeijiTask = function() {
            window.updateHuangchengWeijiStatus('正在停止皇城危机...', true);
            window.updateHelperText('正在停止皇城危机...');
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'stop_huangcheng_weiji_task'
                });
            }
        };

        window.updateT)HTML" \
R"HTML(askZoneStatus = function(text, isRunning) {
            var statusEl = document.getElementById('task-zone-status');
            if (statusEl) {
                statusEl.textContent = text;
                statusEl.style.color = isRunning ? 'var(--color-primary)' : 'var(--color-text-secondary)';
            }
            window.isTaskZoneRunning = isRunning;

            var startBtn = document.getElementById('start-task-zone');
            var stopBtn = document.getElementById('stop-task-zone');
            if (startBtn) {
                startBtn.disabled = isRunning;
                startBtn.textContent = isRunning ? '执行中...' : '开始';
            }
            if (stopBtn) {
                stopBtn.disabled = !isRunning;
                stopBtn.textContent = '停止';
            }
        };

        window.startTaskZone = function() {
            if (window.isTaskZoneRunning) return;

            window.updateTaskZoneStatus('正在执行任务区，请勿操作...', true);
            window.updateHelperText(')HTML" \
R"HTML(正在执行任务区，请勿操作...');

            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'task_zone'
                });
            }
        };

        window.stopTaskZone = function() {
            if (!window.isTaskZoneRunning) return;

            window.updateHelperText('正在停止任务区...');
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'stop_task_zone'
                });
            }
        };

        // 妖力考验 - 一键完成
        window.oneKeyAct631 = function() {
            // act631 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act631'
                }));
            }
        };

        // 果宝救援 - 一键完成
        window.oneKeyAct757 = function() {
    )HTML" \
R"HTML(        // act757 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act757'
                }));
            }
        };

        // 航海大挑战 - 一键完成
        window.oneKeyAct685 = function() {
            // act685 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act685'
                }));
            }
        };

        // 天之骄子的特训 - 一键完成
        window.oneKeyAct666 = function() {
            // act666 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act666'
)HTML" \
R"HTML(                }));
            }
        };

        // 欢乐跷跷板 - 一键完成
        window.oneKeyAct808 = function() {
            // act808 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act808'
                }));
            }
        };

        // 清除煞气 - 一键完成
        window.oneKeyAct641 = function() {
            // act641 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act641'
                }));
            }
        };

        // 驱赶毒蚊 - 一键完成
        window.oneKeyAct717 = function() {
            // act717 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) )HTML" \
R"HTML({
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act717'
                }));
            }
        };

        // 土幻的曙光 - 一键完成
        window.oneKeyAct684 = function() {
            // act684 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act684'
                }));
            }
        };

        // 守护梦境 - 一键完成
        window.oneKeyAct805 = function() {
            // act805 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act805'
                }));
            }
        };

        // 摘取大力果实 - 一键完成
        window.oneKeyAct782 = function() {
            // act782 已并入“最新)HTML" \
R"HTML(活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act782'
                }));
            }
        };

        // 我是神射手 - 一键完成
        window.oneKeyAct811 = function() {
            // act811 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act811'
                }));
            }
        };

        // 圣石之战 - 一键完成
        window.oneKeyAct826 = function() {
            // act826 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act826'
                }));
      )HTML" \
R"HTML(      }
        };

        // 齐心协力大挑战 - 一键完成
        window.oneKeyAct827 = function() {
            // act827 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act827'
                }));
            }
        };

        // 光辉穿梭大考验 - 一键完成
        window.oneKeyAct822 = function() {
            // act822 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act822'
                }));
            }
        };

        // 逆流的试炼 - 一键完成
        window.oneKeyAct804 = function() {
            // act804 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                win)HTML" \
R"HTML(dow.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act804',
                    score: 450
                }));
            }
        };

        // 疾速特训 - 一键完成
        window.oneKeyAct810 = function() {
            // act810 已并入“最新活动”勾选式批量执行；此处保留兼容入口。
            // 扫荡与否由 C++ 侧按历史最好成绩自动判定。
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'one_key_act810'
                }));
            }
        };

        // ====================================================================
        // 最新活动：勾选式批量执行（与材料采集同一交互模型）
        // 剩余次数由 C++ 侧通过 window.updateActivityCounts 回填；次数为 0
        // 时自动禁用该行，避免无效操作。
        // ====================================================================
        window.actLatestIds = [647, 869, 749, 870, 704, 697];

        // C++ 回填各活动剩余次数：{ "717": 3, "822": 0, ... }
        // 值为负数或 null 表示未知，显示“--”且不禁用。
        window.u)HTML" \
R"HTML(pdateActivityCounts = function(counts) {
            if (!counts) return;
            window.actLatestCounts = window.actLatestCounts || {};
            window.actLatestIds.forEach(function(id) {
                var raw = counts[id];
                if (raw === undefined) raw = counts[String(id)];
                if (raw === undefined) return;
                window.actLatestCounts[id] = raw;
                window.applyActivityCount(id, raw);
            });
        };

        window.applyActivityCount = function(id, raw) {
            var countEl = document.getElementById('act-count-' + id);
            var rowEl = document.getElementById('act-row-' + id);
            var pickEl = document.getElementById('act-check-' + id);
            if (!countEl || !rowEl) return;

            var n = (raw === null || raw === undefined) ? null : Number(raw);
            if (n === null || isNaN(n) || n < 0) {
                // 未知：显示占位，保持可勾选
                countEl.textContent = '--';
            )HTML" \
R"HTML(    countEl.className = 'act-item-count act-count-unknown';
                rowEl.classList.remove('act-disabled');
                if (pickEl) pickEl.disabled = false;
                return;
            }

            countEl.textContent = '剩 ' + n;
            if (n <= 0) {
                countEl.className = 'act-item-count act-count-zero';
                rowEl.classList.add('act-disabled');
                if (pickEl) { pickEl.checked = false; pickEl.disabled = true; }
            } else {
                countEl.className = 'act-item-count';
                rowEl.classList.remove('act-disabled');
                if (pickEl) pickEl.disabled = false;
            }
        };

        // 全选 / 全不选（跳过已禁用的行）
        window.selectAllActivities = function(checked) {
            window.actLatestIds.forEach(function(id) {
                var pickEl = document.getElementById('act-check-' + id);
                if (pickEl && !pickEl.disabled) pickEl.checked = checked;
            });
      )HTML" \
R"HTML(  };

        // 请求刷新各活动剩余次数（C++ 侧逐个查询活动信息后回填）
        window.refreshActivityCounts = function() {
            window.updateHelperText('正在获取活动剩余次数...');
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'refresh_activity_counts'
                }));
            }
        };

        // 一键执行所勾选的活动：逐个走 one_key_latestNNN 通道
        window.runSelectedActivities = function() {
            var picked = [];
            window.actLatestIds.forEach(function(id) {
                var pickEl = document.getElementById('act-check-' + id);
                if (pickEl && pickEl.checked && !pickEl.disabled) picked.push(id);
            });

            if (picked.length === 0) {
                window.updateHelperText('请至少勾选一个活动');
                return;
            }

            if (!(window.chrome && window.chrome.webview)) return;

            window.updateHelperText('开始执行 ' + picked.length + ' 个)HTML" \
R"HTML(活动...');
            picked.forEach(function(id, index) {
                // 依次下发，避免多个活动线程同时抢占响应等待。
                // 扫荡与否由 C++ 侧自动判定（可扫荡且扫荡分数不低于游戏流程时自动扫荡）。
                setTimeout(function() {
                    window.chrome.webview.postMessage(JSON.stringify({
                        action: 'one_key_latest' + id
                    }));
                }, index * 300);
            });
        };

        window.heavenFuruiRunning = false;

        // 福瑞宝箱 - 开始
        window.startHeavenFurui = function() {
            if (window.heavenFuruiRunning) return;
            
            var maxBoxes = parseInt(document.getElementById('heaven-furui-max').value) || 30;
            if (maxBoxes < 1) maxBoxes = 1;
            if (maxBoxes > 30) maxBoxes = 30;
            document.getElementById('heaven-furui-max').value = maxBoxes;
            document.getElementById('heaven-furui-target').textContent = maxBoxes;
            document.getElementById('heaven-furui-count').textContent = '0)HTML" \
R"HTML(';
            
            window.heavenFuruiRunning = true;
            
            // 更新按钮状态
            document.getElementById('start-heaven-furui').disabled = true;
            document.getElementById('stop-heaven-furui').disabled = false;
            
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'start_heaven_furui',
                    maxBoxes: maxBoxes
                }));
            }
        };

        // 福瑞宝箱 - 停止
        window.stopHeavenFurui = function() {
            if (!window.heavenFuruiRunning) return;
            
            window.heavenFuruiRunning = false;
            
            // 更新按钮状态
            document.getElementById('start-heaven-furui').disabled = false;
            document.getElementById('stop-heaven-furui').disabled = true;
            
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postM)HTML" \
R"HTML(essage(JSON.stringify({ 
                    action: 'stop_heaven_furui'
                }));
            }
        };

        // 福瑞宝箱 - 更新进度（C++调用）
        window.updateHeavenFuruiProgress = function(current, total) {
            document.getElementById('heaven-furui-count').textContent = current;
            document.getElementById('heaven-furui-target').textContent = total;
        };

        // 福瑞宝箱 - 完成（C++调用）
        window.onHeavenFuruiComplete = function() {
            window.heavenFuruiRunning = false;
            document.getElementById('start-heaven-furui').disabled = false;
            document.getElementById('stop-heaven-furui').disabled = true;
        };

        // 坐骑大赛 - 一键完成
        window.oneKeyHorseCompetition = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    action: 'one_key_horse_competition'
                }));
            }
        };

        windo)HTML" \
R"HTML(w.stopHorseCompetition = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'stop_horse_competition'
                }));
            }
        };

        // 坐骑大赛 - 更新状态（C++调用）
        window.updateHorseCompetitionStatus = function(status) {
            var statusEl = document.getElementById('horse-competition-status');
            if (statusEl) {
                statusEl.textContent = status;
            }
        };

        window.updateHorseCompetitionPoints = function(points) {
            var pointsEl = document.getElementById('horse-competition-points');
            if (pointsEl) {
                pointsEl.textContent = points;
            }
        };

        // 跳舞大赛 - 计数器
        window.danceGameData = {
            completedCount: 0,  // 已完成次数
            maxCount: 3         // 最大次数
        };

        // 一键跳舞
        window.completeDanceTasks = function() {
  )HTML" \
R"HTML(          if (window.isDailyTaskRunning) return;
            
            if (!document.getElementById('daily-dance').checked) {
                window.updateDailyTaskStatus('请勾选自动跳舞', false);
                return;
            }
            
            // 重置计数，每次点击都从0开始
            window.danceGameData.completedCount = 0;
            var el = document.getElementById('dance-count');
            if (el) {
                el.textContent = '0';
            }
            
            window.updateDailyTaskStatus('正在执行跳舞大赛...', true);
            window.updateHelperText('正在执行跳舞大赛...');
            
            // 发送到C++端
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'daily_tasks',
                    flags: 0x100  // 跳舞大赛
                });
            }
        };

        // 更新跳舞次数显示
        window.updateDanceCount = function(count) {
            window.danceGameData.completedCount = count;
      )HTML" \
R"HTML(      var el = document.getElementById('dance-count');
            if (el) {
                el.textContent = count;
            }
        };

        // 一键采集功能
        window.selectAllCollect = function(checked) {
            for (var i = 0; i < 16; i++) {
                var checkbox = document.getElementById('collect-' + i);
                if (checkbox) {
                    checkbox.checked = checked;
                }
            }
        };

        window.startOneKeyCollect = function() {
            // 收集选中的采集物品
            var flags = 0;
            for (var i = 0; i < 16; i++) {
                var checkbox = document.getElementById('collect-' + i);
                if (checkbox && checkbox.checked) {
                    flags |= (1 << i);
                }
            }
            
            if (flags === 0) {
                window.updateHelperText('请至少勾选一项采集物品');
                return;
            }
            
            window.updateHelperText('开始一键采集...');
      )HTML" \
R"HTML(      
            // 发送到C++端
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'one_key_collect',
                    flags: flags
                });
            }
        };

        // 跳舞完成回调 - 计数由服务器响应更新，这里只更新状态提示
        window.onDanceComplete = function() {
            // 计数已由 updateDanceCount 更新（从服务器响应获取）
            var count = window.danceGameData.completedCount;
            
            if (count >= window.danceGameData.maxCount) {
                window.updateDailyTaskStatus('跳舞大赛已完成全部3次', false);
                window.updateHelperText('跳舞大赛已完成全部3次');
            } else {
                window.updateDailyTaskStatus('跳舞完成，已完成 ' + count + '/3 次', false);
                window.updateHelperText('跳舞完成，已完成 ' + count + '/3 次');
            }
        };

        // 辅助日志管理
        window.helperLogData = [];

        // 添加辅助日志
        window.addHelperLog = function(text) {
            var logList)HTML" \
R"HTML( = document.getElementById('helper-log-list');
            var logCount = document.getElementById('helper-log-count');
            if (!logList) return;

            // 如果是第一条日志，清空"暂无日志记录"提示
            if (window.helperLogData.length === 0) {
                logList.innerHTML = '';
            }

            // 创建时间戳
            var now = new Date();
            var timeStr = now.getHours().toString().padStart(2, '0') + ':' +
                         now.getMinutes().toString().padStart(2, '0') + ':' +
                         now.getSeconds().toString().padStart(2, '0');

            // 添加到数据数组
            var logItem = {
                index: window.helperLogData.length + 1,
                time: timeStr,
                content: text
            };
            window.helperLogData.push(logItem);

            // 添加到UI
            var logDiv = document.createElement('div');
            logDiv.style.display = 'grid';
            logDiv.style.gridTemplateColumns = '60px 90px 1fr';
   )HTML" \
R"HTML(         logDiv.style.padding = '3px 8px';
            logDiv.style.borderBottom = '1px solid var(--color-border)';
            logDiv.style.alignItems = 'center';
            logDiv.style.fontSize = '10.5px';
            logDiv.style.color = 'var(--color-text-primary)';

            logDiv.innerHTML = `
                <div style="text-align: center; color: var(--color-text-secondary); font-weight: 500;">${logItem.index}</div>
                <div style="text-align: center; color: var(--color-text-secondary);">${logItem.time}</div>
                <div style="text-align: left; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">${logItem.content}</div>
            `;

            logList.appendChild(logDiv);
            logList.scrollTop = logList.scrollHeight;

            // 更新统计信息
            if (logCount) {
                logCount.textContent = '总记录数: ' + window.helperLogData.length;
            }
        };

        // 清空辅助日志
        window.clearHelperLog = function)HTML" \
R"HTML(() {
            window.helperLogData = [];
            var logList = document.getElementById('helper-log-list');
            var logCount = document.getElementById('helper-log-count');
            if (logList) {
                logList.innerHTML = '<div style="text-align: center; padding: 30px; color: var(--color-text-tertiary);">暂无日志记录</div>';
            }
            if (logCount) {
                logCount.textContent = '总记录数: 0';
            }
        };

        // 更新辅助提示文本
        window.updateHelperText = function(text) {
            var el = document.getElementById('helper-text');
            if (el) {
                el.textContent = text;
                el.classList.remove('slide-in');
                void el.offsetWidth; // 触发重绘
                el.classList.add('slide-in');
            }

            // 同时添加到辅助日志
            window.addHelperLog(text);
        };

        // 日常活动完成回调
        window.onDailyTaskComplete = function(completedCount, totalCount) {
            wi)HTML" \
R"HTML(ndow.updateDailyTaskStatus('已完成 ' + completedCount + '/' + totalCount + ' 项日常活动', false);
            window.updateHelperText('已完成 ' + completedCount + '/' + totalCount + ' 项日常活动');
        };

        // 试炼完成回调
        window.onTrialComplete = function(trialName) {
            window.updateDailyTaskStatus(trialName + ' 完成', false);
            window.updateHelperText(trialName + ' 完成');
        };

        // 全选/全不选
        window.selectAllDaily = function() {
            var checkboxes = ['daily-deep-dig', 'daily-dance', 'daily-fire-wind', 'daily-fire', 
                             'daily-storm', 'daily-card', 'daily-gift', 'weekly-gift',
                             'family-checkin', 'family-report', 'family-defend', 'shop-surprise'];
            checkboxes.forEach(function(id) {
                var cb = document.getElementById(id);
                if (cb) cb.checked = true;
            });
        };

        window.deselectAllDaily = function() {
            var checkboxes = ['da)HTML" \
R"HTML(ily-deep-dig', 'daily-dance', 'daily-fire-wind', 'daily-fire', 
                             'daily-storm', 'daily-card', 'daily-gift', 'weekly-gift',
                             'family-checkin', 'family-report', 'family-defend', 'shop-surprise'];
            checkboxes.forEach(function(id) {
                var cb = document.getElementById(id);
                if (cb) cb.checked = false;
            });
        };

        // 绑定所有按钮事件
        document.addEventListener('DOMContentLoaded', function() {
            // 标题栏拖拽功能
            var titleBar = document.querySelector('.title-bar');
            if (titleBar) {
                titleBar.addEventListener('mousedown', function(e) {
                    // 检查是否点击在控制按钮上
                    if (e.target.closest('.title-bar-button')) {
                        return; // 不处理控制按钮区域的拖拽
                    }
                    // 发送拖拽消息
                    window.chrome.webview.postMessage({ type: 'window-drag' });
                });
     )HTML" \
R"HTML(       }
            
            // 标题栏按钮事件
            var btnMinimize = document.getElementById('btn-minimize');
            if (btnMinimize) {
                btnMinimize.addEventListener('click', function() {
                    window.chrome.webview.postMessage({ type: 'window-minimize' });
                });
            }
            
            var btnClose = document.getElementById('btn-close');
            if (btnClose) {
                btnClose.addEventListener('click', function() {
                    window.chrome.webview.postMessage({ type: 'window-close' });
                });
            }
            
            // 日常活动按钮
            var completeBtn = document.getElementById('complete-daily-tasks');
            if (completeBtn) {
                completeBtn.addEventListener('click', window.completeDailyTasks);
            }
            var trialBtn = document.getElementById('complete-trial-tasks');
            if (trialBtn) {
                trialBtn.addEventListe)HTML" \
R"HTML(ner('click', window.completeTrialTasks);
            }
            var danceBtn = document.getElementById('complete-dance-tasks');
            if (danceBtn) {
                danceBtn.addEventListener('click', window.completeDanceTasks);
            }
            var startTaskZoneBtn = document.getElementById('start-task-zone');
            if (startTaskZoneBtn) {
                startTaskZoneBtn.addEventListener('click', window.startTaskZone);
            }
            var stopTaskZoneBtn = document.getElementById('stop-task-zone');
            if (stopTaskZoneBtn) {
                stopTaskZoneBtn.addEventListener('click', window.stopTaskZone);
            }
            var huangchengHideCompleted = document.getElementById('huangcheng-hide-completed');
            if (huangchengHideCompleted) {
                huangchengHideCompleted.addEventListener('change', function() {
                    window.toggleHuangchengWeijiHideCompleted(this.checked);
                });
            }
 )HTML" \
R"HTML(           var refreshHuangchengBtn = document.getElementById('refresh-huangcheng-weiji');
            if (refreshHuangchengBtn) {
                refreshHuangchengBtn.addEventListener('click', window.refreshHuangchengWeijiTaskProgress);
            }
            var selectAllHuangchengBtn = document.getElementById('select-all-huangcheng-weiji');
            if (selectAllHuangchengBtn) {
                selectAllHuangchengBtn.addEventListener('click', window.selectAllHuangchengWeiji);
            }
            var clearHuangchengBtn = document.getElementById('clear-huangcheng-weiji');
            if (clearHuangchengBtn) {
                clearHuangchengBtn.addEventListener('click', window.clearHuangchengWeijiSelection);
            }
            var startHuangchengBtn = document.getElementById('start-huangcheng-weiji');
            if (startHuangchengBtn) {
                startHuangchengBtn.addEventListener('click', window.startHuangchengWeijiTask);
            }
            var stopH)HTML" \
R"HTML(uangchengBtn = document.getElementById('stop-huangcheng-weiji');
            if (stopHuangchengBtn) {
                stopHuangchengBtn.addEventListener('click', window.stopHuangchengWeijiTask);
            }
            var xuanttaBtn = document.getElementById('one-key-xuantta');
            if (xuanttaBtn) {
                xuanttaBtn.addEventListener('click', window.oneKeyXuantta);
            }
            var buyDice18Btn = document.getElementById('buy-dice-18');
            if (buyDice18Btn) {
                buyDice18Btn.addEventListener('click', window.buyDice18);
            }
            var startShuangTaiBtn = document.getElementById('start-shuangtai');
            if (startShuangTaiBtn) {
                startShuangTaiBtn.addEventListener('click', window.startShuangTai);
            }
            var queryShuangTaiBtn = document.getElementById('query-shuangtai');
            if (queryShuangTaiBtn) {
                queryShuangTaiBtn.addEventListener('click', window.queryShu)HTML" \
R"HTML(angTai);
            }
            var stopShuangTaiBtn = document.getElementById('stop-shuangtai');
            if (stopShuangTaiBtn) {
                stopShuangTaiBtn.addEventListener('click', window.stopShuangTai);
            }
            var strawberryBtn = document.getElementById('one-key-strawberry');
            if (strawberryBtn) {
                strawberryBtn.addEventListener('click', window.oneKeyStrawberry);
            }
            var act631Btn = document.getElementById('one-key-act631');
            if (act631Btn) {
                act631Btn.addEventListener('click', window.oneKeyAct631);
            }
            var act685Btn = document.getElementById('one-key-act685');
            if (act685Btn) {
                act685Btn.addEventListener('click', window.oneKeyAct685);
            }
            var act757Btn = document.getElementById('one-key-act757');
            if (act757Btn) {
                act757Btn.addEventListener('click', window.oneKeyAct757);
         )HTML" \
R"HTML(   }
            var act666Btn = document.getElementById('one-key-act666');
            if (act666Btn) {
                act666Btn.addEventListener('click', window.oneKeyAct666);
            }
            var act808Btn = document.getElementById('one-key-act808');
            if (act808Btn) {
                act808Btn.addEventListener('click', window.oneKeyAct808);
            }
            var act641Btn = document.getElementById('one-key-act641');
            if (act641Btn) {
                act641Btn.addEventListener('click', window.oneKeyAct641);
            }
            var act684Btn = document.getElementById('one-key-act684');
            if (act684Btn) {
                act684Btn.addEventListener('click', window.oneKeyAct684);
            }
            var act717Btn = document.getElementById('one-key-act717');
            if (act717Btn) {
                act717Btn.addEventListener('click', window.oneKeyAct717);
            }
            var act805Btn = document.getElementById('on)HTML" \
R"HTML(e-key-act805');
            if (act805Btn) {
                act805Btn.addEventListener('click', window.oneKeyAct805);
            }
            var act804Btn = document.getElementById('one-key-act804');
            if (act804Btn) {
                act804Btn.addEventListener('click', window.oneKeyAct804);
            }
            var act810Btn = document.getElementById('one-key-act810');
            if (act810Btn) {
                act810Btn.addEventListener('click', window.oneKeyAct810);
            }
            var act811Btn = document.getElementById('one-key-act811');
            if (act811Btn) {
                act811Btn.addEventListener('click', window.oneKeyAct811);
            }
            var act826Btn = document.getElementById('one-key-act826');
            if (act826Btn) {
                act826Btn.addEventListener('click', window.oneKeyAct826);
            }
            var act827Btn = document.getElementById('one-key-act827');
            if (act827Btn) {
               )HTML" \
R"HTML( act827Btn.addEventListener('click', window.oneKeyAct827);
            }
            var act822Btn = document.getElementById('one-key-act822');
            if (act822Btn) {
                act822Btn.addEventListener('click', window.oneKeyAct822);
            }
            var act782Btn = document.getElementById('one-key-act782');
            if (act782Btn) {
                act782Btn.addEventListener('click', window.oneKeyAct782);
            }
            var startHeavenFuruiBtn = document.getElementById('start-heaven-furui');
            if (startHeavenFuruiBtn) {
                startHeavenFuruiBtn.addEventListener('click', window.startHeavenFurui);
            }
            var stopHeavenFuruiBtn = document.getElementById('stop-heaven-furui');
            if (stopHeavenFuruiBtn) {
                stopHeavenFuruiBtn.addEventListener('click', window.stopHeavenFurui);
            }
            var horseCompetitionBtn = document.getElementById('one-key-horse-competition');
            )HTML" \
R"HTML(if (horseCompetitionBtn) {
                horseCompetitionBtn.addEventListener('click', window.oneKeyHorseCompetition);
            }
            var stopHorseCompetitionBtn = document.getElementById('stop-horse-competition');
            if (stopHorseCompetitionBtn) {
                stopHorseCompetitionBtn.addEventListener('click', window.stopHorseCompetition);
            }
            if (typeof window.renderHuangchengWeijiTasks === 'function') {
                window.renderHuangchengWeijiTasks();
            }
        });

        window.decomposeLingyu = function() {
            // 保留此函数以兼容旧代码，现在重定向到批量分解
            window.decomposeAllInQueue();
        };

        window.applyLingyuSort = function() {
            if (!window.currentLingyuData || !window.currentLingyuData.items) return;
            
            const attr = document.getElementById('lingyu-sort-attr').value;
            // 默认按从高到低排序，如果用户想要更多选项后续可以再加
            const order = 'desc'; 
            
            let)HTML" \
R"HTML( sortedItems = [...window.currentLingyuData.items];
            
            if (attr !== 'none') {
                sortedItems.sort((a, b) => {
                    const valA = (a.nativeList.find(n => n.nativeName === attr) || {nativeValue: 0}).nativeValue;
                    const valB = (b.nativeList.find(n => n.nativeName === attr) || {nativeValue: 0}).nativeValue;
                    return order === 'desc' ? valB - valA : valA - valB;
                });
            }
            
            window.renderLingyuList(sortedItems);
        };

        // 封包窗口显示/隐藏功能
        var packetWindow = document.getElementById('packet-window');
        var togglePacketBtn = document.getElementById('toggle-packet-window');
        var closePacketBtn = document.getElementById('close-packet-window');
        var clearAllBtn = document.getElementById('clear-all-packets');
        var deleteSelectedBtn = document.getElementById('delete-selected-packets');
        
        // 显示/隐藏封包窗口
        if )HTML" \
R"HTML((togglePacketBtn) {
            togglePacketBtn.addEventListener('click', function() {
                if (packetWindow.style.display === 'none' || packetWindow.style.display === '') {
                    // 显示窗口
                    packetWindow.style.display = 'block';
                    // 添加动画效果
                    setTimeout(function() {
                        packetWindow.style.opacity = '1';
                        packetWindow.style.transform = 'translateY(0)';
                        // 获取窗口位置和大小
                        var rect = packetWindow.getBoundingClientRect();
                        // 发送postMessage通知C++端弹窗显示
                        if (window.chrome && window.chrome.webview) {
                            window.chrome.webview.postMessage({
                                type: 'packet_window_visible',
                                visible: true,
                                left: rect.left,
                                top: rect.top,
                        )HTML" \
R"HTML(        width: rect.width,
                                height: rect.height
                            });
                        }
                    }, 10);
                    // 更改按钮文本
                    togglePacketBtn.textContent = '关闭';
                } else {
                    // 隐藏窗口
                    packetWindow.style.opacity = '0';
                    // 添加动画效果
                    setTimeout(function() {
                        packetWindow.style.display = 'none';
                        // 更改按钮文本
                        togglePacketBtn.textContent = '显示封包';
                        // 发送postMessage通知C++端弹窗隐藏
                        if (window.chrome && window.chrome.webview) {
                            window.chrome.webview.postMessage({
                                type: 'packet_window_visible',
                                visible: false
                            });
                        }
                    }, 200);
                    packetWin)HTML" \
R"HTML(dow.style.transform = 'translateY(0)';
                }
            });
        }
        
        // 关闭封包窗口
        if (closePacketBtn) {
            closePacketBtn.addEventListener('click', function() {
                packetWindow.style.opacity = '0';
                // 添加动画效果
                setTimeout(function() {
                    packetWindow.style.display = 'none';
                    // 更改按钮文本
                    togglePacketBtn.textContent = '显示封包';
                    // 发送postMessage通知C++端弹窗隐藏
                    if (window.chrome && window.chrome.webview) {
                        window.chrome.webview.postMessage({
                            type: 'packet_window_visible',
                            visible: false
                        });
}
                }, 200);
                packetWindow.style.transform = 'translateY(0)';
            });
        }
        
        // 清空所有封包
        if (clearAllBtn) {
            clearAllBtn.addEventListener('click', function()HTML" \
R"HTML() {
                // 清空逻辑已内置在HTML的onclick属性中
            });
        }

        // 删除选中封包
        if (deleteSelectedBtn) {
            deleteSelectedBtn.addEventListener('click', function() {
                var pListItems = document.getElementById('packet-list-items');
                if (pListItems) {
                    // 找到所有选中的封包项
                    var selectedItems = pListItems.querySelectorAll('.packet-item[style*="background"]');
                    if (selectedItems.length === 0) {
                        alert('请先选中要删除的封包');
                        return;
                    }

                    // 收集选中的索引
                    var selectedIndices = [];
                    selectedItems.forEach(function(item) {
                        var index = item.getAttribute('data-index');
                        if (index) {
                            selectedIndices.push(parseInt(index));
                        }
                    });

                    // 从UI中删除
         )HTML" \
R"HTML(           selectedItems.forEach(function(item) {
                        item.remove();
                    });

                    // 通知C++端删除封包
                    if (window.chrome && window.chrome.webview && selectedIndices.length > 0) {
                        window.chrome.webview.postMessage({
                            type: 'delete_selected_packets',
                            indices: selectedIndices
                        });
                    }

                    // 更新计数
                    var countElement = document.getElementById('packet-count');
                    if (countElement) {
                        countElement.textContent = pListItems.children.length;
                    }
                }
            });
        }
        
        // 变速功能实现
        var speedSlider = document.getElementById('speed-slider');
        var speedValue = document.getElementById('speed-value');
        
        if (speedSlider) {
            speedSlider.addEventListener('i)HTML" \
R"HTML(nput', function() {
                var val = parseFloat(this.value).toFixed(1);
                speedValue.textContent = val + 'X';
                
                // 缓动特效：这里可以添加一些UI上的视觉反馈，比如改变阴影或颜色深度
                speedValue.style.transform = 'scale(1.1)';
                setTimeout(() => {
                    speedValue.style.transform = 'scale(1)';
                }, 100);

                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type: 'set_speed',
                        speed: parseFloat(val)
                    });
                }
            });
        }
        
        // 自动回血功能实现
        window.toggleAutoHeal = function(enabled) {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'toggle_auto_heal',
                    enabled: enabled
                });
            }
        };

        // 万妖盛会 / 八卦)HTML" \
R"HTML(灵盘共用自动战斗功能实现
        window.toggleBattleSixAutoBattle = function(enabled) {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'battlesix_set_auto_battle',
                    enabled: enabled
                });
            }
        };

        // 屏蔽战斗功能实现
        window.toggleBlockBattle = function(enabled) {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'set_block_battle',
                    enabled: enabled
                });
            }
        };

        // 自动回家功能实现
        window.toggleAutoGoHome = function(enabled) {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'set_auto_go_home',
                    enabled: enabled
                });
            }
        };

        // 封包列表功能实现
        var packetList = docu)HTML" \
R"HTML(ment.getElementById('packet-list');
        var packetHexInput = document.getElementById('packet-hex-input');
        var sendPacketBtn = document.getElementById('send-packet-btn');
        var startInterceptBtn = document.getElementById('start-intercept');
        var stopInterceptBtn = document.getElementById('stop-intercept');
        var interceptSendCb = document.getElementById('intercept-send');
        var interceptReceiveCb = document.getElementById('intercept-receive');
        var clearAllBtn = document.getElementById('clear-all-packets');
        var deleteSelectedBtn = document.getElementById('delete-selected-packets');
        var packetCountElement = document.getElementById('packet-count');
        
        // 点击列表项时的选中效果和数据填充
        if (packetList) {
            packetList.addEventListener('click', function(e) {
                var pListItems = document.getElementById('packet-list-items');
                if (!pListItems) return;
                
                var cli)HTML" \
R"HTML(ckedItem = e.target.closest('.packet-item');
                if (clickedItem) {
                    // 移除之前的选中状态
                    var allItems = pListItems.querySelectorAll('.packet-item');
                    allItems.forEach(function(item) {
                        item.classList.remove('selected');
                    });
                    
                    // 添加当前项的选中状态
                    clickedItem.classList.add('selected');
                    
                    // 将16进制字符串显示到编辑框
                    var hexData = clickedItem.getAttribute('data-hex');
                    if (packetHexInput) {
                        packetHexInput.value = hexData;
                    }
                }
            });
        }
        
        // 发送封包功能
        if (sendPacketBtn) {
            console.log('[Debug] 发送按钮已找到，绑定事件监听器');
            sendPacketBtn.addEventListener('click', function(e) {
                console.log('[Debug] 发送按钮被点击');
                e.preventDefault();
   )HTML" \
R"HTML(             var hexData = packetHexInput ? packetHexInput.value : '';
                console.log('[Debug] 封包数据: ' + hexData);
                if (!hexData.trim()) {
                    window.updateHelperText('请输入要发送的封包数据');
                    return;
                }
                // 发送postMessage通知C++端发送封包
                if (window.chrome && window.chrome.webview) {
                    console.log('[Debug] 发送postMessage到C++端');
                    window.chrome.webview.postMessage({
                        type: 'send_packet',
                        hex: hexData
                    });
                    // 显示发送提示（C++端会处理实际发送结果）
                    window.updateHelperText('封包已发送');
                } else {
                    console.log('[Debug] window.chrome.webview 不存在');
                }
            });
        } else {
            console.log('[Debug] 发送按钮未找到');
        }
        
        // 删除选中封包功能
        if (deleteSelectedBtn) {
            deleteSelectedBtn.addEve)HTML" \
R"HTML(ntListener('click', function() {
                var pListItems = document.getElementById('packet-list-items');
                if (!pListItems) return;
                
                var selectedItems = pListItems.querySelectorAll('.packet-item.selected');
                var indices = [];
                selectedItems.forEach(function(item) {
                    var index = parseInt(item.getAttribute('data-index'), 10);
                    if (!isNaN(index)) {
                        indices.push(index - 1);
                    }
                    item.remove();
                });
                // 清空编辑框
                if (packetHexInput) {
                    packetHexInput.value = '';
                }
                
                // 更新封包计数
                updatePacketCount();
                
                // 通知后端删除这些索引
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type: 'd)HTML" \
R"HTML(elete_selected_packets',
                        indices: indices
                    });
                }
            });
        }

        // 开始拦截按钮
        if (startInterceptBtn) {
            startInterceptBtn.addEventListener('click', function() {
                // 发送postMessage通知C++端开始拦截
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('start_intercept');
                }

                // 更新按钮状态
                startInterceptBtn.style.background = 'var(--color-background)';
                startInterceptBtn.style.color = 'var(--color-text-primary)';
                startInterceptBtn.style.borderColor = 'var(--color-border)';
                startInterceptBtn.disabled = true;

                stopInterceptBtn.style.background = 'var(--color-primary)';
                stopInterceptBtn.style.color = 'white';
                stopInterceptBtn.style.borderColor = 'var(--color-primary)';
                stopInterc)HTML" \
R"HTML(eptBtn.disabled = false;
            });
        }

        // 停止拦截按钮
        if (stopInterceptBtn) {
            stopInterceptBtn.addEventListener('click', function() {
                // 发送postMessage通知C++端停止拦截
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('stop_intercept');
                }

                // 更新按钮状态
                stopInterceptBtn.style.background = 'var(--color-background)';
                stopInterceptBtn.style.color = 'var(--color-text-primary)';
                stopInterceptBtn.style.borderColor = 'var(--color-border)';
                stopInterceptBtn.disabled = true;

                startInterceptBtn.style.background = 'var(--color-primary)';
                startInterceptBtn.style.color = 'white';
                startInterceptBtn.style.borderColor = 'var(--color-primary)';
                startInterceptBtn.disabled = false;
            });
        }

        // 拦截类型复选框
        if (int)HTML" \
R"HTML(erceptSendCb && interceptReceiveCb) {
            interceptSendCb.addEventListener('change', function() {
                updateInterceptType();
            });

            interceptReceiveCb.addEventListener('change', function() {
                updateInterceptType();
            });
        }

        // 更新拦截类型函数
        function updateInterceptType() {
            if (interceptSendCb && interceptReceiveCb) {
                // 发送postMessage通知C++端更新拦截类型
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type: 'set_intercept_type',
                        send: interceptSendCb.checked,
                        recv: interceptReceiveCb.checked
                    });
                }
            }
        }

// 清空封包列表
        if (clearAllBtn) {
            clearAllBtn.addEventListener('click', function() {
                var pListItems = document.getElementById('packet-list-items');
          )HTML" \
R"HTML(      if (pListItems) {
                    pListItems.innerHTML = '';
                }
                
                // 清空编辑框
                if (packetHexInput) {
                    packetHexInput.value = '';
                }
                
                // 更新封包计数
                updatePacketCount();
                
                // 发送postMessage通知C++端清空封包
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('clear_packets');
                }
            });
        }

        // 更新封包计数函数
        window.updatePacketCount = function(count) {
            var countElement = document.getElementById('packet-count');
            var packetListItems = document.getElementById('packet-list-items');
            if (countElement) {
                if (count !== undefined) {
                    countElement.textContent = count;
                } else {
                    var packetCount = packetListItems ? packetListIt)HTML" \
R"HTML(ems.children.length : 0;
                    countElement.textContent = packetCount;
                }
            }
        };

        // ==================== 劫持功能实现 ====================
        var hijackEnabledCb = document.getElementById('hijack-enabled');
        var hijackSendCb = document.getElementById('hijack-send');
        var hijackRecvCb = document.getElementById('hijack-recv');
        var hijackPatternInput = document.getElementById('hijack-pattern');
        var hijackReplaceInput = document.getElementById('hijack-replace');
        var hijackTypeSelect = document.getElementById('hijack-type');

        // 自动劫持功能 - 当复选框状态改变时自动发送劫持设置
        function sendHijackSettings() {
            var pattern = hijackPatternInput ? hijackPatternInput.value.trim() : '';
            var replace = hijackReplaceInput ? hijackReplaceInput.value.trim() : '';
            var isSend = hijackSendCb ? hijackSendCb.checked : true;
            var isRecv = hijackRecvCb ? hijackRecvCb.checked : )HTML" \
R"HTML(true;
            var hijackType = hijackTypeSelect ? hijackTypeSelect.value : 'block';

            // 如果没有检测文本，则不发送劫持规则
            if (!pattern) {
                return;
            }

            // 如果是替换模式但没有替换文本，则不发送劫持规则
            if (hijackType === 'replace' && !replace) {
                return;
            }

            // 如果两个复选框都没有选中，则清空劫持规则
            if (!isSend && !isRecv) {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage('clear_hijack_rules');
                }
                return;
            }

            // 发送劫持规则
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'add_hijack_rule',
                    rule: {
                        isSend: isSend,
                        isBlock: (hijackType === 'block'),
                        pattern: pattern,
                        replace: replace
                    )HTML" \
R"HTML(}
                });
            }
        }

        // 为复选框添加 change 事件监听器
        if (hijackSendCb) {
            hijackSendCb.addEventListener('change', sendHijackSettings);
        }

        if (hijackRecvCb) {
            hijackRecvCb.addEventListener('change', sendHijackSettings);
        }

        // 为输入框添加 input 事件监听器，实时更新劫持设置
        if (hijackPatternInput) {
            hijackPatternInput.addEventListener('input', sendHijackSettings);
        }

        if (hijackReplaceInput) {
            hijackReplaceInput.addEventListener('input', sendHijackSettings);
        }

        // 为劫持类型选择框添加 change 事件监听器
        if (hijackTypeSelect) {
            hijackTypeSelect.addEventListener('change', sendHijackSettings);
        }

        // ==================== 保存/载入封包功能实现 ====================
        var savePacketsBtn = document.getElementById('save-packets');
        var loadPacketsBtn = document.getElementById('load-packets');
        var sendAllPacketsBtn = document.getElementBy)HTML" \
R"HTML(Id('start-send');

        // 保存封包
        if (savePacketsBtn) {
            savePacketsBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
                    // 直接通知C++端打开保存对话框
                    window.chrome.webview.postMessage({
                        type: 'save_packet_list'
                    });
                }
            });
        }

        // 载入封包
        if (loadPacketsBtn) {
            loadPacketsBtn.addEventListener('click', function() {
                if (window.chrome && window.chrome.webview) {
                    // 直接通知C++端打开载入对话框
                    window.chrome.webview.postMessage({
                        type: 'load_packet_list'
                    });
                }
            });
        }

        // 自动发送所有封包
        if (sendAllPacketsBtn) {
            sendAllPacketsBtn.addEventListener('click', function() {
                // 获取发送设置
                var sendCountInput = document.getElementByI)HTML" \
R"HTML(d('send-count');
                var sendDelayInput = document.getElementById('send-delay');
                var sendCount = sendCountInput ? parseInt(sendCountInput.value) || 1 : 1;
                var sendDelay = sendDelayInput ? parseInt(sendDelayInput.value) || 300 : 300;

                // 发送配置到C++端
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type: 'send_all_packets',
                        sendCount: sendCount,
                        sendDelay: sendDelay
                    });
                }
            });
        }

        // 停止发送封包
        var stopSendBtn = document.getElementById('stop-send');
        if (stopSendBtn) {
            stopSendBtn.addEventListener('click', function() {
                // 通知C++端停止发送
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage({
                        type: 'stop_send'
   )HTML" \
R"HTML(                 });
                }
            });
        }

        // 监听来自C++的消息，用于添加封包
        if (window.chrome && window.chrome.webview) {
            window.chrome.webview.addEventListener('message', function(event) {
                if (event.data && typeof event.data === 'object' && event.data.type === 'add_packet') {
                    addPacketToUI(event.data.direction, event.data.hex, event.data.time, event.data.label || '');
                }
            });
        }

        // 初始页面加载时，触发第一个标签页的动画
        window.onload = function() {
            console.log('[UI] Page loaded, checking window functions...');
            console.log('[UI] window.updateBattleUI:', typeof window.updateBattleUI);
            console.log('[UI] window.addBattleData:', typeof window.addBattleData);
            
            var activeTab = document.querySelector('.tab-content.active');
            if (activeTab) {
                reanimateTabContent(activeTab);
            }
        };

    )HTML" \
R"HTML(    /**
         * 简化 Buff 名称显示
         * @param {string} name - 原始名称
         * @param {number} round - 回合数
         * @returns {string} 简化后的名称
         */
        function simplifyBufName(name, round) {
            if (!name) return '状态';
            
            let simplified = name;
            
            // 处理 "攻击提升X级" -> "攻击+X"
            const upMatch = name.match(/(攻击|防御|速度|命中|暴击|法术|抗性|体力)提升(\d+)级?/);
            if (upMatch) {
                simplified = upMatch[1] + '+' + upMatch[2];
            }
            
            // 处理 "攻击降低X级" -> "攻击-X"
            const downMatch = name.match(/(攻击|防御|速度|命中|暴击|法术|抗性|体力)降低(\d+)级?/);
            if (downMatch) {
                simplified = downMatch[1] + '-' + downMatch[2];
            }
            
            // 处理其他升降格式
            simplified = simplified.replace('提升', '+').replace('降低', '-').replace('级', '');
            
            // 追加回合数（如果有）
            if (round > 0) {
                simplified += '(' + round + ')')HTML" \
R"HTML(;
            }
            
            return simplified;
        }

        /**
         * 渲染 Buff 标签列表
         * @param {Array} bufArr - Buff 数组
         * @param {HTMLElement} container - 容器元素
         */
        function renderBufList(bufArr, container) {
            container.innerHTML = '';
            if (!bufArr || bufArr.length === 0) {
                return;
            }

            // 用于去重的 Map：key = bufId, value = buf 对象
            const bufMap = new Map();
            
            // 首先处理去重：同类型 Buff 只保留最新的
            bufArr.forEach(buf => {
                // 跳过瞬时/PP 事件 (3, 4, 5, 6)，这些不显示为状态标签
                if (buf.addOrRemove === 3 || buf.addOrRemove === 4 ||
                    buf.addOrRemove === 5 || buf.addOrRemove === 6) {
                    return;
                }

                // 移除类型 (0)：从 Map 中删除
                if (buf.addOrRemove === 0) {
                    if (buf.bufId) {
                        bufMap.delete(buf.bufId);
                    })HTML" \
R"HTML(
                    return;
                }

                // 添加/更新类型（addOrRemove 为 1 或 2）：放入 Map
                // 注意：回合开始时 addOrRemove 可能保持为 1，这表示已存在的 buf
                if (buf.addOrRemove === 1 || buf.addOrRemove === 2) {
                    if (buf.bufId) {
                        bufMap.set(buf.bufId, buf);
                    }
                    return;
                }

                // 如果 addOrRemove 未设置或为其他值（回合开始更新的 buf），根据 bufId 处理
                if (buf.bufId) {
                    // 更新 Map 中已有的 buf
                    bufMap.set(buf.bufId, buf);
                }
            });

            // 渲染去重后的 Buff
            bufMap.forEach(buf => {
                const tag = document.createElement('span');
                tag.className = 'buf-tag';
                
                // 根据 bufId 判断是增益还是减益
                const buffIds = [2, 9, 17, 24, 29, 33, 34, 59, 95, 36, 37, 46, 45, 62, 9999];
                const isBuff = buffIds.includes(buf.bufId) || 
        )HTML" \
R"HTML(                       (buf.name && (
                                   buf.name.includes('攻击') && !buf.name.includes('降') && !buf.name.includes('减') ||
                                   buf.name.includes('防御') && !buf.name.includes('降') && !buf.name.includes('减') ||
                                   buf.name.includes('速度') && !buf.name.includes('降') && !buf.name.includes('减') ||
                                   buf.name.includes('命中') ||
                                   buf.name.includes('暴击') ||
                                   buf.name.includes('回血') ||
                                   buf.name.includes('护盾') ||
                                   buf.name.includes('提升')
                               ));
                
                // 减益关键词
                const debuffKeywords = ['灼烧', '中毒', '流血', '麻痹', '冰冻', '睡眠', '混乱', '恐惧', '诅咒', '虚弱', '减速', '降低'];
                const isDebuff = debuffKeywords.some(kw => buf.name && buf.name.includes(kw));

                // 设置)HTML" \
R"HTML(样式
                if (isDebuff) {
                    tag.style.background = 'rgba(244, 67, 54, 0.15)';
                    tag.style.color = '#d32f2f';
                    tag.style.border = '1px solid rgba(244, 67, 54, 0.3)';
                } else if (isBuff) {
                    tag.style.background = 'rgba(76, 175, 80, 0.15)';
                    tag.style.color = '#388e3c';
                    tag.style.border = '1px solid rgba(76, 175, 80, 0.3)';
                } else {
                    tag.style.background = 'rgba(158, 158, 158, 0.15)';
                    tag.style.color = '#616161';
                    tag.style.border = '1px solid rgba(158, 158, 158, 0.3)';
                }

                tag.style.fontSize = '8px';
                tag.style.padding = '1px 4px';
                tag.style.borderRadius = '2px';
                tag.style.cursor = 'pointer';
                tag.style.whiteSpace = 'nowrap';
                tag.style.flexShrink = '0';  // 防止标签被压缩

       )HTML" \
R"HTML(         // 简化显示名称
                const displayText = simplifyBufName(buf.name, buf.round);
                tag.textContent = displayText;

                // 构建 tooltip 内容（显示完整信息）
                let tooltipContent = '';
                if (buf.name) {
                    tooltipContent += '<b>' + buf.name + '</b>';
                }
                if (buf.round > 0) {
                    tooltipContent += '<br>剩余: ' + buf.round + ' 回合';
                }
                if (buf.tipString) {
                    tooltipContent += '<br>' + buf.tipString;
                }
                if (buf.bufId) {
                    tooltipContent += '<br><span style="color:#888;font-size:8px;">ID: ' + buf.bufId + '</span>';
                }

                if (tooltipContent) {
                    tag.title = tooltipContent.replace(/<br>/g, '\n').replace(/<[^>]*>/g, '');
                    tag.setAttribute('data-tooltip', tooltipContent);
                    
                    // 添加悬停事件显)HTML" \
R"HTML(示详细 tooltip
                    tag.addEventListener('mouseenter', function(e) {
                        showBufTooltip(e, this);
                    });
                    tag.addEventListener('mouseleave', function() {
                        hideBufTooltip();
                    });
                }

                container.appendChild(tag);
            });
            
            // 延迟计算宽度，确保标签已完全渲染
            setTimeout(() => {
                // 计算所有标签的总宽度
                let totalWidth = 0;
                const tags = container.querySelectorAll('.buf-tag');
                tags.forEach(tag => {
                    totalWidth += tag.offsetWidth;
                });
                const gapWidth = 2; // gap: 2px
                const gapCount = Math.max(0, tags.length - 1);
                totalWidth += gapCount * gapWidth;
                
                const containerWidth = container.clientWidth;
                console.log('buf宽度计算 - 总宽度:', totalWidth, '容器宽度:', conta)HTML" \
R"HTML(inerWidth, '标签数量:', tags.length);
                
                // 只有当总宽度超过容器宽度时才显示滚动条
                if (totalWidth > containerWidth) {
                    container.style.overflowX = 'auto';
                    container.classList.add('overflow-auto');
                    console.log('✓ 显示滚动条');
                } else {
                    container.style.overflowX = 'hidden';
                    container.classList.remove('overflow-auto');
                    console.log('✓ 隐藏滚动条');
                }
            }, 10);  // 延迟10ms确保DOM渲染完成
        }

        /**
         * 显示 Buff 详细 Tooltip
         */
        function showBufTooltip(event, element) {
            hideBufTooltip(); // 先移除已有的

            const tooltip = document.createElement('div');
            tooltip.id = 'buf-tooltip';
            tooltip.style.cssText = `
                position: fixed;
                background: rgba(40, 40, 40, 0.95);
                color: #fff;
                padding: 8px 10px;
     )HTML" \
R"HTML(           border-radius: 4px;
                font-size: 10px;
                line-height: 1.5;
                max-width: 200px;
                z-index: 10000;
                box-shadow: 0 2px 8px rgba(0,0,0,0.3);
                pointer-events: none;
            `;

            const content = element.getAttribute('data-tooltip');
            if (content) {
                tooltip.innerHTML = content;
            }

            document.body.appendChild(tooltip);

            // 定位
            const rect = element.getBoundingClientRect();
            let left = rect.left;
            let top = rect.bottom + 4;

            // 防止超出屏幕
            if (left + tooltip.offsetWidth > window.innerWidth) {
                left = window.innerWidth - tooltip.offsetWidth - 8;
            }
            if (top + tooltip.offsetHeight > window.innerHeight) {
                top = rect.top - tooltip.offsetHeight - 4;
            }

            tooltip.style.left = left + 'px';
            toolti)HTML" \
R"HTML(p.style.top = top + 'px';
        }

        /**
         * 隐藏 Buff Tooltip
         */
        function hideBufTooltip() {
            const existing = document.getElementById('buf-tooltip');
            if (existing) {
                existing.remove();
            }
        }

        /**
         * 更新战斗UI数据
         * @param {Object} data - 战斗数据对象
         */
        window.updateBattleUI = function(data) {
            if (!data) data = window._battleData;
            if (!data) return;
            window._battleData = data;
            
            // 调试输出：确认函数被调用
            console.log('[Battle] updateBattleUI called with data:', data);

            // 更新我方数据
            if (data.myPets && data.myPets.length > 0) {
                const myActive = data.myPets[data.myActiveIndex] || data.myPets[0];
                document.getElementById('my-spirit-id').textContent = myActive.spiritId;
                document.getElementById('my-hp').textContent = myActive.hp + '/' + myActive.max)HTML" \
R"HTML(Hp;
                document.getElementById('my-name').textContent = myActive.name ? myActive.name : ('妖怪' + myActive.spiritId);

                // 更新技能列表
                const mySkillList = document.getElementById('my-skill-list');
                mySkillList.innerHTML = '';
                (Array.isArray(myActive.skills) ? myActive.skills : []).forEach(skill => {
                    const row = document.createElement('div');
                    row.className = 'skill-row';
                    row.innerHTML = `
                        <div style="font-size: 9px; color: var(--color-text-primary); text-align: center;">${skill.name ? skill.name : ('技能' + skill.id)}</div>
                        <div style="font-size: 9px; color: var(--color-text-primary); text-align: center;">${skill.pp}/${skill.maxPp}</div>
                    `;
                    mySkillList.appendChild(row);
                });

                // 更新我方 Buff 标签
                const myBufList = document.getElementBy)HTML" \
R"HTML(Id('my-buf-list');
                if (myBufList) {
                    renderBufList(myActive.bufArr, myBufList);
                }

                // 更新我方组合框
                const mySelect = document.getElementById('my-pet-select');
                mySelect.innerHTML = '';
                data.myPets.forEach((pet, index) => {
                    const option = document.createElement('option');
                    option.value = index;
                    option.textContent = pet.name ? pet.name : ('妖怪' + pet.spiritId);
                    if (index === data.myActiveIndex) option.selected = true;
                    mySelect.appendChild(option);
                });
                mySelect.value = String(data.myActiveIndex);
            } else {
                // Clear UI
                document.getElementById('my-spirit-id').textContent = '0';
                document.getElementById('my-hp').textContent = '0/0';
                document.getElementById('my-name').textContent = '未知')HTML" \
R"HTML(;
                document.getElementById('my-skill-list').innerHTML = '';
                document.getElementById('my-pet-select').innerHTML = '<option value="0">妖怪1</option>';
                const myBufList = document.getElementById('my-buf-list');
                if (myBufList) myBufList.innerHTML = '';
            }

            // 更新敌方数据
            if (data.otherPets && data.otherPets.length > 0) {
                const otherActive = data.otherPets[data.otherActiveIndex] || data.otherPets[0];
                document.getElementById('other-spirit-id').textContent = otherActive.spiritId;
                document.getElementById('other-hp').textContent = otherActive.hp + '/' + otherActive.maxHp;
                document.getElementById('other-name').textContent = otherActive.name ? otherActive.name : ('妖怪' + otherActive.spiritId);

                // 更新技能列表
                const otherSkillList = document.getElementById('other-skill-list');
                otherSkillList.innerHTML = ')HTML" \
R"HTML(';
                (Array.isArray(otherActive.skills) ? otherActive.skills : []).forEach(skill => {
                    const row = document.createElement('div');
                    row.className = 'skill-row';
                    row.innerHTML = `
                        <div style="font-size: 9px; color: var(--color-text-primary); text-align: center;">${skill.name ? skill.name : ('技能' + skill.id)}</div>
                        <div style="font-size: 9px; color: var(--color-text-primary); text-align: center;">${skill.pp}/${skill.maxPp}</div>
                    `;
                    otherSkillList.appendChild(row);
                });

                // 更新敌方 Buff 标签
                const otherBufList = document.getElementById('other-buf-list');
                if (otherBufList) {
                    renderBufList(otherActive.bufArr, otherBufList);
                }

                // 更新敌方组合框
                const otherSelect = document.getElementById('other-pet-select');
         )HTML" \
R"HTML(       otherSelect.innerHTML = '';
                data.otherPets.forEach((pet, index) => {
                    const option = document.createElement('option');
                    option.value = index;
                    option.textContent = pet.name ? pet.name : ('妖怪' + pet.spiritId);
                    if (index === data.otherActiveIndex) option.selected = true;
                    otherSelect.appendChild(option);
                });
                otherSelect.value = String(data.otherActiveIndex);
            } else {
                // Clear UI
                document.getElementById('other-spirit-id').textContent = '0';
                document.getElementById('other-hp').textContent = '0/0';
                document.getElementById('other-name').textContent = '未知';
                document.getElementById('other-skill-list').innerHTML = '';
                document.getElementById('other-pet-select').innerHTML = '<option value="0">敌方妖怪1</option>';
                const otherBufLi)HTML" \
R"HTML(st = document.getElementById('other-buf-list');
                if (otherBufList) otherBufList.innerHTML = '';
            }
        };

        (function() {
            var mySelectEl = document.getElementById('my-pet-select');
            if (mySelectEl) {
                mySelectEl.addEventListener('change', function() {
                    var d = window._battleData;
                    if (!d || !Array.isArray(d.myPets) || d.myPets.length === 0) return;
                    var idx = parseInt(this.value, 10);
                    if (isNaN(idx)) idx = 0;
                    if (idx < 0) idx = 0;
                    if (idx >= d.myPets.length) idx = d.myPets.length - 1;
                    d.myActiveIndex = idx;
                    window.updateBattleUI(d);
                });
            }
            var otherSelectEl = document.getElementById('other-pet-select');
            if (otherSelectEl) {
                otherSelectEl.addEventListener('change', function() {
               )HTML" \
R"HTML(     var d = window._battleData;
                    if (!d || !Array.isArray(d.otherPets) || d.otherPets.length === 0) return;
                    var idx = parseInt(this.value, 10);
                    if (isNaN(idx)) idx = 0;
                    if (idx < 0) idx = 0;
                    if (idx >= d.otherPets.length) idx = d.otherPets.length - 1;
                    d.otherActiveIndex = idx;
                    window.updateBattleUI(d);
                });
            }
        })();

        /**
         * 添加战斗数据到UI列表
         * @param {string} type - 消息类型 (如: '战斗开始', '回合结算')
         * @param {string} data - 数据内容描述
         */
        window.addBattleData = function(type, data) {
            // 获取我方和敌方的技能列表容器 (这里以我方为例进行演示，实际应根据 type 解析)
            // 假设我们将所有解析到的战斗事件添加到一个通用的记录列表，或者更新现有的属性
            
            // 查找战斗数据标签页中的列表容器
            var battleTab = document.getElementById('battle');
            if (!battleTab) return;

            // 示例：更新辅助提示区域
            var helperTe)HTML" \
R"HTML(xt = document.getElementById('helper-text');
            if (helperText) {
                helperText.textContent = "[" + type + "] " + data;
                helperText.classList.remove('slide-in');
                void helperText.offsetWidth; // 触发重绘
                helperText.classList.add('slide-in');
            }
        };

        // ============== 妖怪背包功能 ==============
        window.currentMonsterData = { monsters: [] };
        window.selectedMonsterIndex = -1;

        // 性格名称映射
        const geniusNames = {
            1: '固执', 2: '保守', 3: '胆小', 4: '开朗', 5: '勇敢',
            6: '冷静', 7: '慎重', 8: '淘气', 9: '孤僻', 10: '调皮',
            11: '急躁', 12: '天真', 13: '温顺', 14: '慎重', 15: '浮躁',
            16: '认真', 17: '坦率', 18: '实干', 19: '害羞', 20: '保守'
        };

        // 性格颜色映射（攻击、防御、法术、抗性、体力、速度）
        const geniusColors = {
            '攻击': '#e74c3c', '防御': '#3498db', '法术': '#9b59b6', 
            '抗性': '#1abc9c', '体力': '#e67e22', '速度': '#2ecc71'
        };

        /**
       )HTML" \
R"HTML(  * 更新妖怪背包UI
         * @param {Object|string} data - 妖怪数据对象或JSON字符串
         */
        window.updateMonsterUI = function(data) {
            try {
                const monsterData = typeof data === 'string' ? JSON.parse(data) : data;
                window.currentMonsterData = monsterData;
                window.renderMonsterList(monsterData.monsters || []);
            } catch (e) {
                console.error('解析妖怪数据失败:', e);
            }
        };

        /**
         * 渲染妖怪列表
         */
        window.renderMonsterList = function(monsters) {
            const listContainer = document.getElementById('monster-list');
            const countElement = document.getElementById('monster-count');
            
            if (!listContainer) return;
            
            listContainer.innerHTML = '';
            if (!monsters || monsters.length === 0) {
                listContainer.innerHTML = '<div style="text-align: center; padding: 20px; color: var(--color-text-tertiary);">暂)HTML" \
R"HTML(无妖怪数据，请打开游戏妖怪背包</div>';
                if (countElement) countElement.textContent = '0';
                return;
            }
            
            if (countElement) countElement.textContent = monsters.length;
            
            monsters.forEach((monster, index) => {
                const row = document.createElement('div');
                row.style.display = 'grid';
                row.style.gridTemplateColumns = '28px 48px 28px 28px 26px 26px 24px 32px 130px 1fr';
                row.style.padding = '3px 4px';
                row.style.borderBottom = '1px solid var(--color-border)';
                row.style.alignItems = 'center';
                row.style.minHeight = '24px';
                row.style.cursor = 'pointer';
                row.style.transition = 'background-color 0.1s linear';
                
                if (window.selectedMonsterIndex === index) {
                    row.style.backgroundColor = 'rgba(0, 120, 212, 0.1)';
                }
              )HTML" \
R"HTML(  
                row.onclick = () => window.selectMonster(index);
                row.onmouseover = () => {
                    if (window.selectedMonsterIndex !== index) {
                        row.style.backgroundColor = 'var(--color-background-hover)';
                    }
                };
                row.onmouseout = () => {
                    if (window.selectedMonsterIndex !== index) {
                        row.style.backgroundColor = 'transparent';
                    } else {
                        row.style.backgroundColor = 'rgba(0, 120, 212, 0.1)';
                    }
                };
                
                // 获取性格名称
                const geniusName = monster.geniusName || geniusNames[monster.geniusType] || '平衡';
                
                // 获取资质名称
                const aptitudeName = monster.aptitudeName || '未知';
                
                // 获取性别名称
                const sexNames = ['未知', '雌', '雄', '无'];
                const sexNam)HTML" \
R"HTML(e = sexNames[monster.sex] || '未知';
                const sexColor = monster.sex === 1 ? '#e91e63' : (monster.sex === 2 ? '#2196f3' : 'var(--color-text-secondary)');
                
                // 格式化技能列表 - 显示所有技能，用/分隔
                let skillNames = '';
                if (monster.skills && monster.skills.length > 0) {
                    skillNames = monster.skills.map(s => s.name || ('技能' + s.id)).join(' / ');
                } else {
                    skillNames = '无';
                }
                
                // 判断是否首发
                const isFirst = monster.isfirst === 1;
                const nameStyle = isFirst ? 'color: var(--color-primary); font-weight: 600;' : 'color: var(--color-text-primary);';
                const firstBadge = isFirst ? '<span style="background: var(--color-primary); color: white; padding: 0 2px; border-radius: 2px; font-size: 7px; margin-left: 2px;">首</span>' : '';
                
                // 系别颜色
                const elemColor )HTML" \
R"HTML(= {
                    '金': '#FFD700', '木': '#228B22', '水': '#1E90FF', '火': '#FF4500', '土': '#8B4513',
                    '妖': '#9400D3', '魔': '#4B0082', '毒': '#32CD32', '圣': '#FFD700', '翼': '#87CEEB',
                    '雷': '#FFD700', '幻': '#DA70D6', '怪': '#696969', '风': '#00CED1', '灵': '#00FA9A'
                };
                const typeColor = elemColor[monster.typeName] || 'var(--color-text-primary)';
                
                // 简化资质名称显示（取前2字）
                const shortAptitude = aptitudeName.length > 2 ? aptitudeName.substring(0, 2) : aptitudeName;
                
                // 构建资质数值显示（顺序：体力/攻击/防御/法术/抗性/速度）
                // geniusList顺序：攻击[0]/防御[1]/法术[2]/抗性[3]/体力[4]/速度[5]
                let aptitudeDisplay = shortAptitude;
                if (monster.geniusList && monster.geniusList.length >= 6) {
                    const hp = monster.geniusList[4].value;      // 体力
                    const atk = monster.geniusList[0].value;     // 攻击
                  )HTML" \
R"HTML(  const def = monster.geniusList[1].value;     // 防御
                    const mag = monster.geniusList[2].value;     // 法术
                    const res = monster.geniusList[3].value;     // 抗性
                    const spd = monster.geniusList[5].value;     // 速度
                    aptitudeDisplay = `${shortAptitude} 体${hp}/攻${atk}/防${def}/法${mag}/抗${res}/速${spd}`;
                }
                
                row.innerHTML = `
                    <div style="text-align: center; color: var(--color-text-secondary);">${index + 1}</div>
                    <div style="text-align: center; ${nameStyle} white-space: nowrap; overflow: hidden; text-overflow: ellipsis;" title="${monster.name || '未知'}">${monster.name || '未知'}</div>
                    <div style="text-align: center; color: var(--color-text-secondary); font-size: 8.5px;">${monster.iid || 0}</div>
                    <div style="text-align: center; color: ${sexColor};">${sexName}</div>
                    <div style="text-)HTML" \
R"HTML(align: center; color: var(--color-text-primary);">${monster.level || 1}</div>
                    <div style="text-align: center; color: #e74c3c;">${monster.hp || 0}</div>
                    <div style="text-align: center; color: ${typeColor}; font-weight: 500;">${monster.typeName || '未知'}</div>
                    <div style="text-align: center; color: #9b59b6;">${geniusName}</div>
                    <div style="text-align: center; color: #e67e22; font-size: 8px; white-space: nowrap;" title="${aptitudeName}">${aptitudeDisplay}</div>
                    <div style="color: var(--color-text-secondary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; padding-left: 4px;" title="${skillNames}">${skillNames}</div>
                `;
                
                listContainer.appendChild(row);
            });
        };

        /**
         * 选择妖怪并显示详情
         */
        window.selectMonster = function(index) {
            window.selectedMonsterIndex = index;
         )HTML" \
R"HTML(   const monsters = window.currentMonsterData.monsters || [];
            const monster = monsters[index];
            
            if (!monster) return;
            
            // 重新渲染列表更新选中状态
            window.renderMonsterList(monsters);
            
            // 更新详情面板
            const detailContainer = document.getElementById('monster-detail');
            if (!detailContainer) return;
            
            const geniusName = monster.geniusName || geniusNames[monster.geniusType] || '平衡';
            const aptitudeName = monster.aptitudeName || '未知';
            const isFirst = monster.isfirst === 1;
            
            // 获取性别
            const sexNames = ['未知', '雌', '雄', '无'];
            const sexName = sexNames[monster.sex] || '未知';
            
            // 构建技能列表
            let skillsHtml = '';
            if (monster.skills && monster.skills.length > 0) {
                monster.skills.forEach(skill => {
                    skillsHtml += `
                   )HTML" \
R"HTML(     <div style="display: flex; justify-content: space-between; padding: 2px 4px; background: var(--color-background-hover); border-radius: 2px; margin-bottom: 2px; font-size: 9.5px;">
                            <span style="font-weight: 500;">${skill.name || ('技能' + skill.id)}</span>
                            <span style="color: var(--color-text-secondary);">PP: ${skill.pp || 0}/${skill.maxPp || 0}</span>
                        </div>
                    `;
                });
            } else {
                skillsHtml = '<div style="text-align: center; color: var(--color-text-tertiary);">无技能数据</div>';
            }
            
            // 构建灵玉列表
            let symmHtml = '';
            if (monster.symmList && monster.symmList.length > 0) {
                monster.symmList.forEach(symm => {
                    symmHtml += `<span style="display: inline-block; background: #e8f5e8; color: #2e7d32; padding: 1px 4px; border-radius: 2px; margin: 1px; font-size: 8.5px;">${symm)HTML" \
R"HTML(.name || ('灵玉' + symm.id)}</span>`;
                });
            } else {
                symmHtml = '<span style="color: var(--color-text-tertiary);">无</span>';
            }
            
            detailContainer.innerHTML = `
                <div style="margin-bottom: 6px; padding: 4px; background: var(--color-background-secondary); border-radius: 3px;">
                    <div style="display: flex; justify-content: space-between; align-items: center;">
                        <span style="font-size: 12.5px; font-weight: 600; color: ${isFirst ? 'var(--color-primary)' : 'var(--color-text-primary)'};">${monster.name || '未知'}</span>
                        ${isFirst ? '<span style="background: var(--color-primary); color: white; padding: 1px 4px; border-radius: 2px; font-size: 8.5px;">首发</span>' : ''}
                    </div>
                    <div style="color: var(--color-text-secondary); font-size: 9.5px;">配置ID: ${monster.iid || 0} | 唯一ID: ${monster.id || 0}</div>
        )HTML" \
R"HTML(            <div style="color: var(--color-text-secondary); font-size: 9.5px;">系别: ${monster.typeName || '未知'} | 性别: ${sexName} | 资质: ${aptitudeName}</div>
                </div>
                
                <div style="margin-bottom: 6px;">
                    <div style="font-weight: 600; color: var(--color-text-primary); margin-bottom: 2px; border-bottom: 1px solid var(--color-border); padding-bottom: 2px;">基础属性</div>
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 2px; font-size: 9.5px;">
                        <div>等级: <span style="font-weight: 500;">Lv${monster.level || 1}</span></div>
                        <div>经验: <span style="font-weight: 500;">${monster.exp || 0}/${monster.needExp || 0}</span></div>
                        <div style="color: #e74c3c;">血量: <span style="font-weight: 500;">${monster.hp || 0}</span></div>
                        <div style="color: #e67e22;">攻击: <span style="font-weight: 500;">${monster.attack || 0}</span)HTML" \
R"HTML(></div>
                        <div style="color: #3498db;">防御: <span style="font-weight: 500;">${monster.defence || 0}</span></div>
                        <div style="color: #9b59b6;">法术: <span style="font-weight: 500;">${monster.magic || 0}</span></div>
                        <div style="color: #1abc9c;">抗性: <span style="font-weight: 500;">${monster.resistance || 0}</span></div>
                        <div style="color: #2ecc71;">速度: <span style="font-weight: 500;">${monster.speed || 0}</span></div>
                    </div>
                </div>
                
                <div style="margin-bottom: 6px;">
                    <div style="font-weight: 600; color: var(--color-text-primary); margin-bottom: 2px; border-bottom: 1px solid var(--color-border); padding-bottom: 2px;">性格: ${geniusName}</div>
                </div>
                
                <div style="margin-bottom: 6px;">
                    <div style="font-weight: 600; color: var(--color-text-primary); ma)HTML" \
R"HTML(rgin-bottom: 2px; border-bottom: 1px solid var(--color-border); padding-bottom: 2px;">资质: ${aptitudeName}</div>
                    <div style="display: grid; grid-template-columns: repeat(3, 1fr); gap: 2px; font-size: 9px;">
                        ${(() => {
                            if (!monster.geniusList || monster.geniusList.length === 0) return '<div style="color: var(--color-text-tertiary);">无资质数据</div>';
                            const starColors = ['', '#9e9e9e', '#4caf50', '#2196f3', '#9c27b0', '#ff9800'];
                            // geniusList顺序：攻击[0]/防御[1]/法术[2]/抗性[3]/体力[4]/速度[5]
                            // 显示顺序：体力/攻击/防御/法术/抗性/速度
                            const displayOrder = [4, 0, 1, 2, 3, 5];
                            return displayOrder.map(idx => {
                                const g = monster.geniusList[idx];
                                const level = g.level - 1; // 星级1-5（减1）
                                const stars = '★'.repeat(level) + '☆'.)HTML" \
R"HTML(repeat(5 - level);
                                return `<div style="padding: 1px 2px; background: var(--color-background-hover); border-radius: 2px;">
                                    <span style="color: var(--color-text-secondary);">${g.name}:</span>
                                    <span style="font-weight: 500; color: ${starColors[level] || 'var(--color-text-primary)'};">${g.value}</span>
                                    <span style="color: ${starColors[level] || 'var(--color-text-tertiary)'}; font-size: 7px;">${stars}</span>
                                </div>`;
                            }).join('');
                        })()}
                    </div>
                </div>
                
                <div style="margin-bottom: 6px;">
                    <div style="font-weight: 600; color: var(--color-text-primary); margin-bottom: 2px; border-bottom: 1px solid var(--color-border); padding-bottom: 2px;">技能 (${monster.skills ? monster.skills.length : 0}个)<)HTML" \
R"HTML(/div>
                    ${skillsHtml}
                </div>
                
                <div>
                    <div style="font-weight: 600; color: var(--color-text-primary); margin-bottom: 2px; border-bottom: 1px solid var(--color-border); padding-bottom: 2px;">灵玉 (${monster.symmList ? monster.symmList.length : 0}个)</div>
                    <div style="display: flex; flex-wrap: wrap;">${symmHtml}</div>
                </div>
            `;
        };

        /**
         * 刷新妖怪列表（发送封包请求）
         */
        window.queryMonsters = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'query_monsters'
                });
            }
        };
        
        // ============== 道具功能 ==============
        window.selectedItemId = 0;      // 当前选中的道具ID
        window.selectedItemName = '';   // 当前选中的道具名称
        window.selectedItemValue = 0;   // 当前选中的道具效果值
        window.itemCount)HTML" \
R"HTML(Map = {};       // 道具数量映射表

        /**
         * 刷新背包物品
         */
        window.refreshPackItems = function() {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'refresh_pack_items'
                });
                window.updateHelperText('正在刷新背包...');
            }
        };

        /**
         * 更新道具数量显示
         * @param {Object} items - 道具数量对象 {itemId: count, ...}
         */
        window.updateItemCounts = function(items) {
            window.itemCountMap = items || {};
            // 更新每个按钮上的数量显示
            document.querySelectorAll('.item-count').forEach(el => {
                var itemId = el.getAttribute('data-id');
                var count = window.itemCountMap[itemId] || 0;
                el.textContent = '(' + count + ')';
                // 数量为0时变红提醒
                if (count === 0) {
                    el.style.color = '#e53935';
                } else {
               )HTML" \
R"HTML(     el.style.color = '';
                }
            });
        };

        /**
         * 选择道具
         * @param {number} itemId - 道具ID
         * @param {string} itemName - 道具名称
         * @param {number} value - 道具效果值
         * @param {string} type - 道具类型 ('hp' 或 'mp')
         */
        window.selectItem = function(itemId, itemName, value, type) {
            window.selectedItemId = itemId;
            window.selectedItemName = itemName;
            window.selectedItemValue = value;
            window.selectedItemType = type || 'hp';
            
            // 更新按钮选中状态
            document.querySelectorAll('.item-btn').forEach(btn => {
                btn.style.background = '#fafafa';
                btn.style.color = '#333';
                btn.style.borderColor = '#e0e0e0';
                btn.style.fontWeight = 'normal';
            });
            
            // 高亮当前选中的按钮
            const currentBtn = document.querySelector('.item-btn[data-id="' + itemId + '"]');
     )HTML" \
R"HTML(       if (currentBtn) {
                currentBtn.style.background = '#e3f2fd';
                currentBtn.style.color = '#1976d2';
                currentBtn.style.borderColor = '#1976d2';
                currentBtn.style.fontWeight = '600';
            }
            
            // 更新选中提示
            const hintEl = document.getElementById('selected-item-hint');
            if (hintEl) {
                var typeText = type === 'mp' ? 'PP' : 'HP';
                hintEl.innerHTML = '<span style="color: var(--color-primary);">✓</span> ' + itemName + ' <span style="color: ' + (type === 'mp' ? '#1976d2' : '#e53935') + ';">+' + value + typeText + '</span>';
            }
            
            var typeText = type === 'mp' ? 'PP' : 'HP';
            window.updateHelperText('已选择: ' + itemName + ' (效果: +' + value + typeText + ')');
        };

        /**
         * 购买选中的道具
         */
        window.buySelectedItem = function() {
            if (!window.selectedItemId) {
                )HTML" \
R"HTML(window.updateHelperText('请先选择要购买的道具');
                return;
            }
            
            const countInput = document.getElementById('item-count-input');
            const count = countInput ? parseInt(countInput.value) || 1 : 1;
            
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'buy_item',
                    itemId: window.selectedItemId,
                    count: count
                });
                window.updateHelperText('正在购买: ' + window.selectedItemName + ' x' + count);
            }
        };

        /**
         * 使用选中的道具（战斗中）
         */
        window.useSelectedItem = function() {
            if (!window.selectedItemId) {
                window.updateHelperText('请先选择要使用的道具');
                return;
            }
            
            const countInput = document.getElementById('item-count-input');
            const count = countInput ? parseInt(countInpu)HTML" \
R"HTML(t.value) || 1 : 1;
            
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'use_item',
                    itemId: window.selectedItemId,
                    count: count
                });
                window.updateHelperText('正在使用: ' + window.selectedItemName);
            }
        };
        
        // ============== BOSS专区功能 ==============
        window.bossList = [];  // 存储BOSS列表
        window.selectedBossId = 0;  // 当前选中的BOSS ID

        /**
         * 初始化BOSS列表（从C++调用）
         * @param {Object|string} data - BOSS数据对象或JSON字符串
         */
        window.initBossList = function(data) {
            try {
                const bossData = typeof data === 'string' ? JSON.parse(data) : data;
                // 支持数组格式或对象格式
                window.bossList = Array.isArray(bossData) ? bossData : (bossData.bosses || []);
                window.renderBossSelect();
            } catch (e) {
  )HTML" \
R"HTML(              console.error('解析BOSS数据失败:', e);
            }
        };

        // 全局变量：BOSS列表缓存
        var cachedBossList = [];  // 缓存BOSS列表，避免重复遍历
        var isSelectingBoss = false;  // 防止选择时触发过滤

        /**
         * 渲染BOSS下拉选择框
         */
        window.renderBossSelect = function() {
            const select = document.getElementById('boss-select');
            if (!select) return;

            // 保存当前选中的值
            const currentValue = select.value;

            // 缓存BOSS列表
            cachedBossList = [...window.bossList];

            select.innerHTML = '';

            // 添加默认选项
            const defaultOption = document.createElement('option');
            defaultOption.value = '0';
            defaultOption.textContent = '-- 请选择BOSS --';
            select.appendChild(defaultOption);

            // 添加BOSS选项
            window.bossList.forEach(boss => {
                const option = document.createElement('option');
                option.value = boss.id;
         )HTML" \
R"HTML(       option.textContent = `${boss.name} (ID: ${boss.id})`;
                select.appendChild(option);
            });

            // 恢复之前选中的值
            if (currentValue && select.querySelector(`option[value="${currentValue}"]`)) {
                select.value = currentValue;
            }
        };

        /**
         * 根据搜索框内容过滤BOSS选项
         */
        window.filterBossSelect = function() {
            // 如果正在选择BOSS，不执行过滤
            if (isSelectingBoss) return;

            const searchInput = document.getElementById('boss-search-input');
            const select = document.getElementById('boss-select');
            if (!searchInput || !select) return;

            const keyword = searchInput.value.toLowerCase().trim();
            const currentValue = select.value;

            // 清空选项
            select.innerHTML = '';

            // 添加默认选项
            const defaultOption = document.createElement('option');
            defaultOption.value = '0';
            defaultOptio)HTML" \
R"HTML(n.textContent = '-- 请选择BOSS --';
            select.appendChild(defaultOption);

            // 过滤并添加BOSS选项
            const filteredBosses = keyword
                ? cachedBossList.filter(boss =>
                    boss.name.toLowerCase().includes(keyword) ||
                    boss.id.toString().includes(keyword)
                )
                : cachedBossList;

            filteredBosses.forEach(boss => {
                const option = document.createElement('option');
                option.value = boss.id;
                option.textContent = `${boss.name} (ID: ${boss.id})`;
                select.appendChild(option);
            });

            // 尝试恢复之前选中的值
            if (currentValue && select.querySelector(`option[value="${currentValue}"]`)) {
                select.value = currentValue;
            }
        };

        /**
         * 选择BOSS
         */
        window.selectBoss = function(bossId) {
            const bossIdNum = parseInt(bossId, 10);
            wind)HTML" \
R"HTML(ow.selectedBossId = bossIdNum;

            const info = document.getElementById('selected-boss-info');

            if (bossIdNum === 0) {
                if (info) info.textContent = '请选择一个BOSS';
                return;
            }

            // 查找BOSS信息
            const boss = window.bossList.find(b => b.id === bossIdNum);
            if (boss) {
                if (info) {
                    info.innerHTML = `已选择: <span style="color: var(--color-primary); font-weight: 600;">${boss.name}</span> (ID: ${boss.id})`;
                }

                // 设置标志位，防止清空搜索框时触发过滤
                isSelectingBoss = true;

                // 清空搜索框，显示所有BOSS列表，避免选中的BOSS消失
                const searchInput = document.getElementById('boss-search-input');
                if (searchInput) {
                    searchInput.value = '';
                }

                // 重新渲染完整的BOSS列表
                const select = document.getElementById('boss-select');
                if (select) {
            )HTML" \
R"HTML(        select.innerHTML = '';
                    
                    // 添加默认选项
                    const defaultOption = document.createElement('option');
                    defaultOption.value = '0';
                    defaultOption.textContent = '-- 请选择BOSS --';
                    select.appendChild(defaultOption);

                    // 添加所有BOSS选项
                    window.bossList.forEach(b => {
                        const option = document.createElement('option');
                        option.value = b.id;
                        option.textContent = `${b.name} (ID: ${b.id})`;
                        select.appendChild(option);
                    });

                    // 设置选中的值
                    select.value = bossIdNum;
                }

                // 延迟重置标志位，确保所有操作完成
                setTimeout(function() {
                    isSelectingBoss = false;
                }, 100);
            }
        };

        /**
         * 进入BOSS战斗
         */
        win)HTML" \
R"HTML(dow.enterBossBattle = function() {
            if (window.selectedBossId === 0) {
                if (window.updateHelperText) {
                    window.updateHelperText('请先选择一个BOSS');
                }
                return;
            }
            
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({
                    type: 'enter_boss_battle',
                    bossId: window.selectedBossId
                });
                
                if (window.updateHelperText) {
                    const boss = window.bossList.find(b => b.id === window.selectedBossId);
                    window.updateHelperText(`正在进入BOSS战斗: ${boss ? boss.name : window.selectedBossId}`);
                }
            }
        };

        /**
         * 显示更新对话框
         */
        window.showUpdateDialog = function(updateInfo) {
            const overlay = document.getElementById('update-dialog-overlay');
            const announcement = d)HTML" \
R"HTML(ocument.getElementById('update-dialog-announcement');
            const content = document.getElementById('update-dialog-content');
            const confirmBtn = document.getElementById('update-dialog-confirm');
            const cancelBtn = document.getElementById('update-dialog-cancel');

            if (!overlay || !announcement || !content || !confirmBtn || !cancelBtn) {
                console.error('Update dialog elements not found');
                return;
            }

            // 隐藏IE浏览框
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'update-dialog-show' });
            }

            // 设置更新内容
            announcement.textContent = updateInfo.announcement || '';
            content.textContent = updateInfo.updateContent || '';

            // 绑定确定按钮事件 - 使用系统默认浏览器打开
            confirmBtn.onclick = function() {
                if (window.chrome && window.chrome.webview) {
                    window.chro)HTML" \
R"HTML(me.webview.postMessage({
                        type: 'open-url',
                        url: updateInfo.downloadUrl
                    });
                }
                hideUpdateDialog();
            };

            // 绑定取消按钮事件
            cancelBtn.onclick = hideUpdateDialog;

            // 显示对话框
            overlay.classList.add('show');
        };

        /**
         * 隐藏更新对话框
         */
        function hideUpdateDialog() {
            const overlay = document.getElementById('update-dialog-overlay');
            if (overlay) {
                overlay.classList.remove('show');
            }
            // 恢复IE浏览框显示
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'update-dialog-hide' });
            }
        }

        /**
         * 显示 Key 登录对话框
         */
        function showKeyLoginDialog() {
            const overlay = document.getElementById('key-login-overlay');
            const input = documen)HTML" \
R"HTML(t.getElementById('key-login-input');
            const confirmBtn = document.getElementById('key-login-confirm');
            const cancelBtn = document.getElementById('key-login-cancel');

            if (!overlay || !input || !confirmBtn || !cancelBtn) {
                console.error('Key login dialog elements not found');
                return;
            }

            // 隐藏IE浏览框
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'key-login-dialog-show' });
            }

            // 清空输入框
            input.value = '';

            // 绑定确定按钮事件
            confirmBtn.onclick = function() {
                const key = input.value.trim();
                if (key) {
                    if (window.chrome && window.chrome.webview) {
                        window.chrome.webview.postMessage({
                            type: 'key-login',
                            key: key
                        });
                 )HTML" \
R"HTML(   }
                }
                hideKeyLoginDialog();
            };

            // 绑定取消按钮事件
            cancelBtn.onclick = hideKeyLoginDialog;

            // 显示对话框
            overlay.classList.add('show');

            // 聚焦输入框
            setTimeout(function() {
                input.focus();
            }, 100);
        }

        /**
         * 隐藏 Key 登录对话框
         */
        function hideKeyLoginDialog() {
            const overlay = document.getElementById('key-login-overlay');
            if (overlay) {
                overlay.classList.remove('show');
            }
            // 恢复IE浏览框显示
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'key-login-dialog-hide' });
            }
        }

        // 副本跳层功能
        function startDungeonJump() {
            const layerInput = document.getElementById('dungeon-layer-input');
            const targetLayer = layerInput ? parseInt(layerInput.value) || 1 : )HTML" \
R"HTML(1;
            
            if (targetLayer < 1 || targetLayer > 9999) {
                updateDungeonStatus('错误：层数必须在1-9999之间', 'error');
                return;
            }
            
            updateDungeonStatus('开始执行副本跳层到第' + targetLayer + '层...', 'info');
            
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'dungeon_jump_start',
                    targetLayer: targetLayer
                });
            }
        }
        
        function stopDungeonJump() {
            updateDungeonStatus('停止副本跳层', 'info');
            
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'dungeon_jump_stop'
                });
            }
        }
        
        function updateDungeonStatus(text, type) {
            const statusText)HTML" \
R"HTML( = document.getElementById('dungeon-status-text');
            if (statusText) {
                statusText.textContent = text;
                
                // 根据类型设置颜色
                if (type === 'error') {
                    statusText.style.color = '#c62828';
                } else if (type === 'success') {
                    statusText.style.color = '#2e7d32';
                } else {
                    statusText.style.color = 'var(--color-text-secondary)';
                }
            }
        }
        
        // 接收C++发送的副本状态更新
        window.updateDungeonJumpStatus = function(text) {
            updateDungeonStatus(text, 'info');
        };

        // ============================================
        // 精魄赠送系统功能
        // ============================================
        
        // 精魄系统状态
        let spiritState = {
            dailyOut: 0,
            weeklyOut: 0,
            spiritList: [],
            selectedSpirit: null,
            lastRecordType: 0
 )HTML" \
R"HTML(       };
        let spiritPendingGift = null;

        function getSpiritDisplayName(spirit) {
            if (!spirit) {
                return '未知精魄';
            }
            const name = spirit.name || ('妖怪ID ' + spirit.eggIid);
            return '【' + name + '】精魄';
        }

        function getSpiritTypeName(type) {
            return type === 1 ? '普通' : type === 2 ? '稀有' : '特殊';
        }

        function getSpiritCharacterName(spirit) {
            if (!spirit) {
                return '未知性格';
            }
            return spirit.characterName || (spirit.character > 0 ? ('性格' + spirit.character) : '未知性格');
        }

        function formatSpiritDate(ts) {
            if (!ts) {
                return '未知时间';
            }
            const date = new Date(ts * 1000);
            if (Number.isNaN(date.getTime())) {
                return '未知时间';
            }
            return date.getFullYear() + '-' +
                String(date.getMonth() + 1).padStart(2, '0') + '-)HTML" \
R"HTML(' +
                String(date.getDate()).padStart(2, '0');
        }

        function getSpiritSkillSummary(spirit) {
            if (!spirit || !Array.isArray(spirit.skillNames) || !spirit.skillNames.length) {
                return '无技能';
            }
            return spirit.skillNames.slice(0, 3).join(' / ');
        }

        function getSpiritSelectedSummary(spirit) {
            if (!spirit) {
                return '无';
            }
            return getSpiritDisplayName(spirit) + '｜' +
                getSpiritTypeName(spirit.eggType) + '｜' +
                (spirit.elemName || '未知系别') + '｜' +
                getSpiritCharacterName(spirit) + '｜' +
                formatSpiritDate(spirit.bornTime) + '｜' +
                getSpiritSkillSummary(spirit);
        }

        function getSpiritHistoryNameById(spiritId) {
            const matched = spiritState.spiritList.find(spirit => spirit.eggIid === spiritId);
            return matched && matched.name ? matched.name : (')HTML" \
R"HTML(ID:' + spiritId);
        }

        function escapeSpiritHtml(text) {
            return String(text == null ? '' : text)
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;')
                .replace(/"/g, '&quot;')
                .replace(/'/g, '&#39;');
        }

        function renderSpiritRecord(text, title) {
            const panel = document.getElementById('spirit-record-panel');
            const titleEl = document.getElementById('spirit-record-title');
            if (titleEl) {
                titleEl.textContent = title || '记录面板';
            }
            if (panel) {
                panel.textContent = text || '暂无记录';
                panel.scrollTop = 0;
            }
        }

        function requestSpiritOpenUi() {
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'spiritCollect',)HTML" \
R"HTML(
                    action: 'open_ui'
                });
            }
        }

        function requestSpiritList() {
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'spiritCollect',
                    action: 'getSpirits'
                });
            }
        }

        function refreshSpiritPanel() {
            updateSpiritStatus('正在刷新精魄数据...', 'info');
            requestSpiritOpenUi();
            setTimeout(function() {
                requestSpiritList();
            }, 80);
        }
        
        // 获取精魄列表
        function getSpiritList() {
            updateSpiritStatus('正在获取精魄列表...', 'info');
            requestSpiritList();
        }
        
        // 显示精魄列表
        function renderSpiritList() {
            const container = document.getElementById('spirit-list');
            if (!container) return;

            if (!spiritState.spiritL)HTML" \
R"HTML(ist.length) {
                spiritState.selectedSpirit = null;
                document.getElementById('spirit-selected').textContent = '无';
                container.innerHTML = '<div class="spirit-list-empty">暂无精魄</div>';
                return;
            }

            if (spiritState.selectedSpirit) {
                const selected = spiritState.spiritList.find(spirit => spirit.eggId === spiritState.selectedSpirit.eggId);
                spiritState.selectedSpirit = selected || null;
            }

            if (spiritState.selectedSpirit) {
                document.getElementById('spirit-selected').textContent = getSpiritSelectedSummary(spiritState.selectedSpirit);
            }
            
            let html = '<div class="spirit-list-grid">';
            spiritState.spiritList.forEach(spirit => {
                const isSelected = spiritState.selectedSpirit && spiritState.selectedSpirit.eggId === spirit.eggId;
                html += '<div class="spirit-item' + (isSelec)HTML" \
R"HTML(ted ? ' selected' : '') + '" onclick="selectSpirit(' + spirit.eggId + ')" title="' + escapeSpiritHtml(getSpiritSkillSummary(spirit)) + '">' +
                    '<div class="spirit-item-top">' +
                        '<span class="spirit-item-name">' + escapeSpiritHtml(getSpiritDisplayName(spirit)) + '</span>' +
                        '<span class="spirit-item-tag">#' + spirit.eggId + '</span>' +
                        '<div class="spirit-item-chips">' +
                            '<span class="spirit-mini-chip">' + escapeSpiritHtml(getSpiritTypeName(spirit.eggType)) + '</span>' +
                            '<span class="spirit-mini-chip">' + escapeSpiritHtml(spirit.elemName || '未知系别') + '</span>' +
                            '<span class="spirit-mini-chip">' + escapeSpiritHtml(getSpiritCharacterName(spirit)) + '</span>' +
                        '</div>' +
                    '</div>' +
                '</div>';
            });
            html += '</div>';
            contain)HTML" \
R"HTML(er.innerHTML = html;
        }
        
        // 选择精魄
        function selectSpirit(eggId) {
            const spirit = spiritState.spiritList.find(s => s.eggId === eggId);
            if (spirit) {
                spiritState.selectedSpirit = spirit;
                document.getElementById('spirit-selected').textContent = getSpiritSelectedSummary(spirit);
                renderSpiritList();
            }
        }
        
        // 验证并赠送精魄
        function verifyAndSendSpirit() {
            const friendIdInput = document.getElementById('spirit-friend-id');
            const friendId = friendIdInput ? friendIdInput.value.trim() : '';
            
            if (!friendId) {
                updateSpiritStatus('请输入对方卡布号', 'error');
                return;
            }
            
            if (!/^\d+$/.test(friendId)) {
                updateSpiritStatus('卡布号只能包含数字', 'error');
                return;
            }
            
            if (!spiritState.selectedSpirit) {
    )HTML" \
R"HTML(            updateSpiritStatus('请先选择要赠送的精魄', 'error');
                return;
            }
            
            if (spiritState.dailyOut <= 0) {
                updateSpiritStatus('今日赠送次数已用完', 'error');
                return;
            }
            
            if (spiritState.weeklyOut <= 0) {
                updateSpiritStatus('本周赠送次数已用完', 'error');
                return;
            }
            
            // 先验证玩家
            updateSpiritStatus('正在验证玩家信息...', 'info');
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'spiritCollect',
                    action: 'verifyPlayer',
                    friendId: parseInt(friendId)
                });
            }
        }
        
        // 获取历史记录
        function getSpiritRecord(type) {
            spiritState.lastRecordType = type;
            updateSpiritStatus('正在获取' + (type === 1 ? '赠送' : '接收') )HTML" \
R"HTML(+ '记录...', 'info');
            renderSpiritRecord('正在加载' + (type === 1 ? '赠送' : '接收') + '记录...', type === 1 ? '赠送记录' : '接收记录');
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                window.chrome.webview.postMessage({
                    type: 'spiritCollect',
                    action: 'history',
                    recordType: type
                });
            }
        }
        
        // 更新精魄系统状态显示
        function updateSpiritStatus(text, type) {
            const statusEl = document.getElementById('spirit-status');
            if (statusEl) {
                statusEl.textContent = text;
                statusEl.style.color = type === 'error' ? '#c62828' : type === 'success' ? '#2e7d32' : 'var(--color-text-tertiary)';
            }
        }

        function hideSpiritConfirmDialog() {
            const overlay = document.getElementById('spirit-confirm-overlay');
            if (overlay) {
                overlay.cla)HTML" \
R"HTML(ssList.remove('show');
            }
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'spirit-confirm-dialog-hide' });
            }
            spiritPendingGift = null;
        }

        function showSpiritConfirmDialog(playerName, friendId) {
            const overlay = document.getElementById('spirit-confirm-overlay');
            const messageEl = document.getElementById('spirit-confirm-message');
            const confirmBtn = document.getElementById('spirit-confirm-accept');
            const cancelBtn = document.getElementById('spirit-confirm-cancel');

            if (!overlay || !messageEl || !confirmBtn || !cancelBtn || !spiritState.selectedSpirit) {
                updateSpiritStatus('确认弹窗初始化失败，请稍后重试', 'error');
                return;
            }

            spiritPendingGift = {
                friendId: friendId,
                playerName: playerName,
                eggId: spiritState.selectedSpirit)HTML" \
R"HTML(.eggId,
                spiritName: getSpiritDisplayName(spiritState.selectedSpirit)
            };

            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage({ type: 'spirit-confirm-dialog-show' });
            }

            messageEl.textContent = '确定要将【' + spiritPendingGift.spiritName + '】赠送给【' + playerName + '】吗？';

            cancelBtn.onclick = function() {
                hideSpiritConfirmDialog();
                updateSpiritStatus('已取消赠送', 'info');
            };
            overlay.onclick = function(event) {
                if (event.target === overlay) {
                    hideSpiritConfirmDialog();
                    updateSpiritStatus('已取消赠送', 'info');
                }
            };

            confirmBtn.onclick = function() {
                const pendingGift = spiritPendingGift;
                hideSpiritConfirmDialog();

                if (!pendingGift) {
                    updateSpiritStatus('赠送数据已失效，请重新验证玩家信)HTML" \
R"HTML(息', 'error');
                    return;
                }

                if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                    window.chrome.webview.postMessage({
                        type: 'spiritCollect',
                        action: 'sendSpirit',
                        friendId: pendingGift.friendId,
                        eggId: pendingGift.eggId
                    });
                    updateSpiritStatus('正在发送精魄...', 'info');
                } else {
                    updateSpiritStatus('当前环境不支持发送精魄请求', 'error');
                }
            };

            updateSpiritStatus('玩家验证成功，请确认赠送对象', 'info');
            overlay.classList.add('show');
        }
        
        // 接收C++发送的精魄系统数据
        window.handleSpiritCollectData = function(data) {
            switch(data.type) {
                case 'spiritState':
                    spiritState.dailyOut = data.data.dOut;
                    spiritState.weeklyOut = d)HTML" \
R"HTML(ata.data.wOut;
                    document.getElementById('spirit-daily-out').textContent = data.data.dOut;
                    document.getElementById('spirit-weekly-out').textContent = data.data.wOut;
                    updateSpiritStatus('赠送状态已更新', 'success');
                    break;
                case 'spiritList':
                    spiritState.spiritList = data.data || [];
                    renderSpiritList();
                    updateSpiritStatus('已获取 ' + spiritState.spiritList.length + ' 个精魄', 'success');
                    break;
                case 'confirm':
                    const friendIdInput = document.getElementById('spirit-friend-id');
                    const friendId = friendIdInput ? parseInt(friendIdInput.value, 10) || 0 : 0;
                    if (!spiritState.selectedSpirit) {
                        updateSpiritStatus('当前没有选中的精魄，请重新选择后再试', 'error');
                        break;
                    }
                    if (friendId <= 0) {
   )HTML" \
R"HTML(                     updateSpiritStatus('卡布号无效，请重新输入后再试', 'error');
                        break;
                    }
                    showSpiritConfirmDialog(data.playerName, friendId);
                    break;
                case 'alert':
                    hideSpiritConfirmDialog();
                    updateSpiritStatus(data.message, 'error');
                    break;
                case 'history':
                    let recordText = data.recordType === 1 ? '赠送记录:\n' : '接收记录:\n';
                    if (data.json) {
                        try {
                            const recordList = JSON.parse(data.json);
                            if (Array.isArray(recordList) && recordList.length > 0) {
                                recordList.forEach(item => {
                                    if (Array.isArray(item) && item.length >= 5) {
                                        const date = new Date(item[3] * 1000);
                                        const dateT)HTML" \
R"HTML(ext = date.getFullYear() + '年' + (date.getMonth() + 1) + '月' + date.getDate() + '日';
                                        const spiritName = getSpiritHistoryNameById(item[2]);
                                        if (item[4] === 2) {
                                            recordText += '【' + dateText + '】【' + item[1] + '】赠送你一个【' + spiritName + '精魄】\n';
                                        } else {
                                            recordText += '【' + dateText + '】你赠送给【' + item[1] + '】一个【' + spiritName + '精魄】\n';
                                        }
                                    }
                                });
                            } else {
                                recordText += '暂无记录';
                            }
                        } catch (e) {
                            recordText += data.json;
                        }
                    } else {
                        recordText += '暂无记录';
                    }
       )HTML" \
R"HTML(             renderSpiritRecord(recordText, data.recordType === 1 ? '赠送记录' : '接收记录');
                    updateSpiritStatus('记录已显示', 'success');
                    break;
                case 'sendSuccess':
                    hideSpiritConfirmDialog();
                    updateSpiritStatus(data.message || '精魄赠送成功！', 'success');
                    // 重新获取精魄列表
                    setTimeout(() => getSpiritList(), 500);
                    break;
            }
        };

        // 万妖盛会PVP功能
        document.addEventListener('DOMContentLoaded', function() {
            // 自动匹配按钮
            const autoMatchBtn = document.getElementById('battlesix-auto-match');
            if (autoMatchBtn) {
                autoMatchBtn.onclick = function() {
                    const matchCountInput = document.getElementById('battlesix-match-count');
                    const matchCount = matchCountInput ? parseInt(matchCountInput.value) || 1 : 1;
                    if (window.chrome && window.chro)HTML" \
R"HTML(me.webview && window.chrome.webview.postMessage) {
                        window.chrome.webview.postMessage({
                            type: 'battlesix_auto_match',
                            matchCount: matchCount
                        });
                    }
                };
            }

            // 取消匹配按钮
            const cancelMatchBtn = document.getElementById('battlesix-cancel-match');
            if (cancelMatchBtn) {
                cancelMatchBtn.onclick = function() {
                    if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                        window.chrome.webview.postMessage({
                            type: 'battlesix_cancel_match'
                        });
                    }
                };
            }

            // 副本跳层按钮
            const dungeonJumpBtn = document.getElementById('dungeon-jump-btn');
            if (dungeonJumpBtn) {
                dungeonJumpBtn.onclick = function() {
    )HTML" \
R"HTML(                startDungeonJump();
                };
            }
            
            // 副本停止按钮
            const dungeonStopBtn = document.getElementById('dungeon-stop-btn');
            if (dungeonStopBtn) {
                dungeonStopBtn.onclick = function() {
                    stopDungeonJump();
                };
            }

            renderSpiritRecord('点击右上角按钮查看赠送记录或接收记录。', '等待加载');
        });

    </script>
</body>
</html>
)HTML"
;
