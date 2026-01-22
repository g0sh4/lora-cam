import serial
import time
import os
import io
import json
from PIL import Image
import numpy as np

# --- KONFIGURACJA ---
SERIAL_PORT = 'COM11'  # Zmień na właściwy port
BAUD_RATE = 115200
START_MARKER = 'START_IMAGE_TRANSFER'
STATS_MARKER = 'STATS_DATA:'

# Foldery i pliki
RESULTS_DIR = "badania_monitoring_january"
STATS_FILE = os.path.join(RESULTS_DIR, "session_stats.json")
os.makedirs(RESULTS_DIR, exist_ok=True)

# Nazewnictwo
BASE_FRAME_PREFIX = "base_frame_"
DIFF_FRAME_PREFIX = "diff_raw_frame"
RECONSTRUCTED_FRAME_PREFIX = "reconstructed_frame"

# Parametry Sparse JPEG
SPARSE_BACKGROUND_COLOR = 0  # 0 dla czarnego tła

# Zmienne globalne (stan)
base_image_pil = None
last_width = 0
last_height = 0
current_frame_stats = None

def log(message):
    """Logowanie z timestampem."""
    timestamp = time.strftime("%H:%M:%S")
    print(f"[{timestamp}] {message}")

def save_stats_to_json(frame_key, stats_data):
    """Zapisuje słownik statystyk do pliku JSON."""
    data = {}
    if os.path.exists(STATS_FILE):
        try:
            with open(STATS_FILE, 'r') as f:
                data = json.load(f)
        except json.JSONDecodeError:
            pass 
    
    data[frame_key] = stats_data
    
    with open(STATS_FILE, 'w') as f:
        json.dump(data, f, indent=4)
    log(f"[JSON] Zapisano statystyki dla klucza: {frame_key}")

def apply_sparse_jpeg(base_img_pil, sparse_jpeg_data, background_color=0):
    """Nakłada sparse JPEG na obraz bazowy."""
    if base_img_pil is None:
        return None
    try:
        sparse_stream = io.BytesIO(sparse_jpeg_data)
        sparse_img_pil = Image.open(sparse_stream).convert('L')
        
        if sparse_img_pil.size != base_img_pil.size:
            log(f"[!] Błąd wymiarów: Base {base_img_pil.size} vs Sparse {sparse_img_pil.size}")
            return None

        base_arr = np.array(base_img_pil, dtype=np.uint8)
        sparse_arr = np.array(sparse_img_pil, dtype=np.uint8)

        # Maska zmienionych pikseli
        changed_pixels_mask = sparse_arr != background_color
        base_arr[changed_pixels_mask] = sparse_arr[changed_pixels_mask]

        return Image.fromarray(base_arr, mode='L')
    except Exception as e:
        log(f"[!] Błąd rekonstrukcji: {e}")
        return None

def main():
    global base_image_pil, last_width, last_height, current_frame_stats

    log(f"--- Odbiornik LoRa Image Receiver ---")
    log(f"Port: {SERIAL_PORT}, Zapis do: {RESULTS_DIR}")

    try:
        with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2) as ser:
            image_counter = 0

            while True:
                try:
                    # Odczyt linii (z ignorowaniem błędów dekodowania dla śmieci)
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if not line: continue

                    # 1. Parsowanie Statystyk
                    if line.startswith(STATS_MARKER):
                        # Format: STATS_DATA:minR,maxR,avgR,minS,maxS,avgS,cnt,ToA,nackCnt,nackHistStr
                        try:
                            clean_line = line.replace(STATS_MARKER, "")
                            vals = clean_line.split(',')
                            
                            if len(vals) >= 10:
                                # Parsowanie historii retransmisji
                                hist_str = vals[9]
                                retrans_hist = []
                                if hist_str != "0":
                                    retrans_hist = [int(x) for x in hist_str.split(';') if x.isdigit()]

                                current_frame_stats = {
                                    "rssi_min": float(vals[0]),
                                    "rssi_max": float(vals[1]),
                                    "rssi_avg": float(vals[2]),
                                    "snr_min": float(vals[3]),
                                    "snr_max": float(vals[4]),
                                    "snr_avg": float(vals[5]),
                                    "frames_cnt": int(vals[6]),
                                    "toa_ms": int(vals[7]),
                                    "retransmissions": {
                                        "count": int(vals[8]),
                                        "history": retrans_hist
                                    }
                                }
                                log(f"[STATS] Odebrano dane telemetryczne. Retransmisje: {retrans_hist}")
                        except ValueError as e:
                            log(f"[STATS] Błąd parsowania: {e}")

                    # 2. Start Transferu Obrazu
                    elif line == START_MARKER:
                        image_counter += 1
                        timestamp = time.strftime("%Y%m%d-%H%M%S")
                        log(f"\n[+] ODBIÓR OBRAZU #{image_counter}...")

                        # Odczyt metadanych: Size, W, H, Block
                        meta_line = ser.readline().decode('utf-8').strip()
                        parts = meta_line.split(',')
                        if len(parts) != 4:
                            log(f"[!] Błąd metadanych: {meta_line}")
                            continue
                        
                        exp_size, width, height, blk_size = map(int, [p.strip() for p in parts])
                        
                        # Odczyt danych binarnych
                        img_data = ser.read(exp_size)
                        if len(img_data) != exp_size:
                            log(f"[!] Niekompletne dane: {len(img_data)}/{exp_size}")
                            continue
                        
                        # --- Przetwarzanie Obrazu ---
                        frame_key = f"frame_{image_counter}_{timestamp}"
                        
                        # Czy to Base Frame? (Pierwsza klatka lub wymuszona zmiana)
                        is_base_frame = (base_image_pil is None)
                        
                        # Jeśli wymiary się zmieniły, traktuj jako Base Frame
                        if not is_base_frame and (width != last_width or height != last_height):
                            log("[!] Zmiana rozdzielczości -> Wymuszenie Base Frame.")
                            is_base_frame = True

                        if is_base_frame:
                            # --- OBSŁUGA BASE FRAME ---
                            try:
                                base_image_pil = Image.open(io.BytesIO(img_data)).convert('L')
                                last_width, last_height = width, height
                                
                                fname = f"{BASE_FRAME_PREFIX}_{timestamp}.png"
                                path = os.path.join(RESULTS_DIR, fname)
                                base_image_pil.save(path)
                                log(f"[FILE] Zapisano BASE: {fname}")
                                
                                if current_frame_stats:
                                    current_frame_stats["type"] = "BASE_FRAME"
                                    current_frame_stats["size_bytes"] = exp_size
                                    
                            except Exception as e:
                                log(f"[!] Błąd BASE: {e}")
                                current_frame_stats = None # Nie zapisuj stats jeśli błąd
                        
                        else:
                            # --- OBSŁUGA DIFF FRAME ---
                            # 1. Zapis surowego diff
                            raw_fname = f"{DIFF_FRAME_PREFIX}_{timestamp}.jpg"
                            with open(os.path.join(RESULTS_DIR, raw_fname), 'wb') as f:
                                f.write(img_data)
                            
                            # 2. Rekonstrukcja
                            updated_img = apply_sparse_jpeg(base_image_pil, img_data, SPARSE_BACKGROUND_COLOR)
                            if updated_img:
                                base_image_pil = updated_img # Aktualizacja bazy
                                recon_fname = f"{RECONSTRUCTED_FRAME_PREFIX}_{timestamp}.png"
                                path = os.path.join(RESULTS_DIR, recon_fname)
                                base_image_pil.save(path)
                                log(f"[FILE] Zapisano RECONSTRUCTED: {recon_fname}")
                                
                                if current_frame_stats:
                                    current_frame_stats["type"] = "DIFF_RECONSTRUCTED"
                                    current_frame_stats["diff_size_bytes"] = exp_size
                            else:
                                log("[!] Błąd rekonstrukcji obrazu.")
                        
                        # --- Zapis Statystyk ---
                        if current_frame_stats:
                            save_stats_to_json(frame_key, current_frame_stats)
                            current_frame_stats = None # Reset
                            
                        log("--- Gotowy ---")

                    # Logowanie innych komunikatów z Node C
                    elif line:
                        log(f"[Node C]: {line}")

                except Exception as loop_e:
                    log(f"Błąd pętli: {loop_e}")
                    time.sleep(0.5)

    except serial.SerialException as e:
        log(f"KRYTYCZNY BŁĄD PORTU: {e}")

if __name__ == '__main__':
    main()