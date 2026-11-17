# OSWiko - finto server Telegram per provare i Messaggi senza internet.
# Uso: vedi tools/mock-telegram.sh. HTTPS su 8443 (per l'emulatore), controllo su 127.0.0.1:8080:
#   curl "http://127.0.0.1:8080/say?text=ciao"   simula un tuo messaggio
#   curl http://127.0.0.1:8080/sent              mostra cosa ha inviato OSWiko
import json, ssl, threading, time, sys
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
updates, sent, uid, mid = [], [], [100], [1]
cv = threading.Condition()
def add_update(text, chat=4242, name="Fabio"):
    with cv:
        uid[0]+=1; mid[0]+=1
        updates.append({"update_id":uid[0],"message":{"message_id":mid[0],"from":{"id":chat,"first_name":name},"chat":{"id":chat,"type":"private"},"date":int(time.time()),"text":text}})
        cv.notify_all()
class H(BaseHTTPRequestHandler):
    protocol_version="HTTP/1.1"
    def log_message(self,*a): pass
    def reply(self, obj, code=200):
        b=json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type","application/json")
        self.send_header("Content-Length",str(len(b))); self.send_header("Connection","close"); self.end_headers(); self.wfile.write(b)
    def do_GET(self): self.handle_req(None)
    def do_POST(self):
        n=int(self.headers.get("Content-Length",0)); self.handle_req(self.rfile.read(n))
    def handle_req(self, body):
        u=urlparse(self.path); q=parse_qs(u.query)
        if self.server.server_port==8080:   # controllo
            if u.path=="/say": add_update(q["text"][0], int(q.get("chat",["4242"])[0])); return self.reply({"ok":True})
            if u.path=="/sent": return self.reply(sent)
            return self.reply({"ok":False},404)
        parts=u.path.split("/")
        if len(parts)<3 or parts[1]!="botTEST123": return self.reply({"ok":False,"error_code":401},401)
        m=parts[2]
        if m=="getUpdates":
            off=int(q.get("offset",["0"])[0]); to=int(q.get("timeout",["0"])[0])
            end=time.time()+min(to,10)
            with cv:
                while True:
                    res=[x for x in updates if x["update_id"]>=off]
                    if res or time.time()>=end: break
                    cv.wait(end-time.time())
            return self.reply({"ok":True,"result":res})
        if m=="sendMessage":
            d=json.loads(body); sent.append(d); print("BOT->", d, flush=True)
            return self.reply({"ok":True,"result":{"message_id":999}})
        self.reply({"ok":False},404)
s=ThreadingHTTPServer(("0.0.0.0",8443),H)
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain("srv.pem","srv.key")
s.socket=ctx.wrap_socket(s.socket, server_side=True)
c=ThreadingHTTPServer(("127.0.0.1",8080),H)
threading.Thread(target=c.serve_forever,daemon=True).start()
print("mock pronto", flush=True); s.serve_forever()
